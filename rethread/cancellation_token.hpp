#ifndef RETHREAD_CANCELLATION_TOKEN_H
#define RETHREAD_CANCELLATION_TOKEN_H

// Copyright (c) 2016, Boris Sazonov
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <rethread/detail/intrusive_list.h>
#include <rethread/detail/reverse_lock.hpp>
#include <rethread/detail/utility.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <algorithm>

namespace rethread
{
	class cancellation_handler
	{
	protected:
		virtual ~cancellation_handler() { }

	public:
		/// @brief Should cancel blocking call
		virtual void cancel() = 0;

		/// @brief Should reset cancellation_handler to it's original state
		///        For each cancel() call, there's _exactly_ one reset() call
		virtual void reset() { }
	};


	class cancellation_token;


	namespace this_thread
	{
		template<typename Rep, typename Period>
		inline void sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token);
	}


	class cancellation_token
	{
	protected:
		mutable std::atomic<cancellation_handler*> _cancel_handler{nullptr};

	public:
		bool is_cancelled() const
		{ return _cancel_handler.load(std::memory_order_relaxed) == HazardPointer(); }

		explicit operator bool() const
		{ return !is_cancelled(); }

	protected:
		virtual void do_sleep_for(const std::chrono::nanoseconds& duration) const = 0;

		/// @pre Handler is registered
		/// @post Handler is not registered
		/// @note Will invoke cancellation_handler::reset() if necessary
		virtual void unregister_cancellation_handler(cancellation_handler& handler) const = 0;

	protected:
		template<typename Rep, typename Period>
		void sleep_for(const std::chrono::duration<Rep, Period>& duration) const
		{ do_sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(duration)); }

		/// @pre Handler is not registered
		/// @returns Whether handler was registered. If token was cancelled before registration, this method skips registration and returns false
		bool try_register_cancellation_handler(cancellation_handler& handler) const
		{
			cancellation_handler* h = _cancel_handler.exchange(&handler, std::memory_order_release);
			RETHREAD_ANNOTATE_BEFORE(std::addressof(_cancel_handler));
			if (RETHREAD_UNLIKELY(h != nullptr))
			{
				RETHREAD_ASSERT(h == HazardPointer(), "Cancellation handler already registered!");
				return false;
			}
			return true;
		}

		/// @pre Handler is registered
		/// @returns Whether unregistration succeed
		bool try_unregister_cancellation_handler(cancellation_handler& handler) const
		{
			cancellation_handler* h = _cancel_handler.exchange(nullptr, std::memory_order_acquire);
			if (RETHREAD_LIKELY(h == &handler))
				return true;

			RETHREAD_ASSERT(h == HazardPointer(), "Another token was registered!");
			_cancel_handler.exchange(h); // restore value
			return false;
		}

	protected:
		cancellation_token() = default;
		cancellation_token(const cancellation_token&) = delete;
		cancellation_token& operator =(const cancellation_token&) = delete;
		~cancellation_token() = default;

		// Dirty trick to optimize register/unregister down to one atomic exchange
		static cancellation_handler* HazardPointer()
		{ return reinterpret_cast<cancellation_handler*>(1); }

	private:
		friend class cancellation_guard_base;

		template<typename Rep, typename Period>
		friend void this_thread::sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token);
	};


	class dummy_cancellation_token : public cancellation_token
	{
	public:
		dummy_cancellation_token() = default;
		// Base class can't be copied, but can default construct it, because dummy_cancellation_token may never cancelled state
		dummy_cancellation_token(const dummy_cancellation_token&) : cancellation_token() { }
		dummy_cancellation_token& operator =(const dummy_cancellation_token&) = delete;
		~dummy_cancellation_token() = default;

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override     { std::this_thread::sleep_for(duration); }

		// Just in case someone skips try_unregister_cancellation_handler and calls this directly
		void unregister_cancellation_handler(cancellation_handler& h) const override
		{
			bool r = try_unregister_cancellation_handler(h);
			(void)r;
			RETHREAD_ASSERT(r, "Dummy cancellation token can't be cancelled!");
		}
	};


	class standalone_cancellation_token : public cancellation_token
	{
		mutable std::mutex              _mutex;
		mutable std::condition_variable _cv;
		bool                            _cancelled{false};
		bool                            _cancel_done{false};

	public:
		standalone_cancellation_token() = default;
		standalone_cancellation_token(const standalone_cancellation_token&) = delete;
		standalone_cancellation_token& operator =(const standalone_cancellation_token&) = delete;
		~standalone_cancellation_token() = default;

		void cancel()
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return;

			_cancelled = true;
			l.unlock();

			cancellation_handler* cancelHandler = _cancel_handler.exchange(HazardPointer());
			RETHREAD_ASSERT(cancelHandler != HazardPointer(), "_cancelled should protect from double-cancelling");

			if (cancelHandler)
			{
				RETHREAD_ANNOTATE_AFTER(std::addressof(_cancel_handler));
				RETHREAD_ANNOTATE_FORGET(std::addressof(_cancel_handler));
				cancelHandler->cancel();
				RETHREAD_ANNOTATE_BEFORE(cancelHandler);
			}

			l.lock();
			_cancel_done = true;
			_cv.notify_all();
		}

		void reset()
		{
			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_ASSERT((_cancel_handler.load() || _cancel_handler == HazardPointer()) && (_cancelled == _cancel_done), "Cancellation token is in use!");
			_cancelled = false;
			_cancel_done = false;
			_cancel_handler = nullptr;
		}

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return;

			_cv.wait_for(l, duration);
		}

		void unregister_cancellation_handler(cancellation_handler& handler) const override
		{
			if (try_unregister_cancellation_handler(handler))
				return;

			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_ASSERT(_cancelled, "Wasn't cancelled!");
			RETHREAD_ASSERT(_cancel_handler.load() == HazardPointer(), "Wrong _cancel_handler");

			while (!_cancel_done)
				_cv.wait(l);

			RETHREAD_ANNOTATE_AFTER(std::addressof(handler));
			RETHREAD_ANNOTATE_FORGET(std::addressof(handler));
			l.unlock();
			handler.reset();
		}
	};


	namespace detail
	{
		template <typename TokenType_>
		struct cancellation_source_data
		{
			using tokens_intrusive_list = detail::intrusive_list<TokenType_>;

			std::mutex              _mutex;
			std::condition_variable _cv;
			bool                    _cancelled{false};
			bool                    _cancel_done{false};
			std::mutex              _tokens_mutex;
			tokens_intrusive_list   _tokens;
		};
	}


	class cancellation_token_source;


	class sourced_cancellation_token : public cancellation_token, public detail::intrusive_list_node
	{
		using data_ptr = std::shared_ptr<detail::cancellation_source_data<sourced_cancellation_token>>;

		data_ptr _data;

	public:
		sourced_cancellation_token(const sourced_cancellation_token& other) :
			cancellation_token(), intrusive_list_node(), _data(other._data)
		{ do_register(); }

		sourced_cancellation_token& operator =(const sourced_cancellation_token&) = delete;

		~sourced_cancellation_token()
		{
			RETHREAD_ASSERT(_cancel_handler.load() == nullptr || _cancel_handler == HazardPointer(), "Cancellation token is still in use!");
			do_unregister();
		}

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override
		{
			std::unique_lock<std::mutex> l(_data->_mutex);
			if (_data->_cancelled)
				return;

			_data->_cv.wait_for(l, duration);
		}

		void unregister_cancellation_handler(cancellation_handler& handler) const override
		{
			if (try_unregister_cancellation_handler(handler))
				return;

			std::unique_lock<std::mutex> l(_data->_mutex);
			RETHREAD_ASSERT(_data->_cancelled, "Wasn't cancelled!");
			RETHREAD_ASSERT(_cancel_handler == HazardPointer(), "Wrong _cancel_handler");

			while (!_data->_cancel_done)
				_data->_cv.wait(l);

			RETHREAD_ANNOTATE_AFTER(std::addressof(handler));
			RETHREAD_ANNOTATE_FORGET(std::addressof(handler));
			l.unlock();
			handler.reset();
		}

	private:
		sourced_cancellation_token(const data_ptr& data) : _data(data)
		{ do_register(); }

		void do_register()
		{
			std::unique_lock<std::mutex> l(_data->_tokens_mutex);
			_data->_tokens.push_back(*this);
		}

		void do_unregister()
		{
			std::unique_lock<std::mutex> l(_data->_tokens_mutex);
			_data->_tokens.erase(*this);
		}

		void cancel_impl(std::unique_lock<std::mutex>& l)
		{
			cancellation_handler* cancelHandler = _cancel_handler.exchange(HazardPointer());
			RETHREAD_ASSERT(cancelHandler != HazardPointer(), "_cancelled should protect from double-cancelling");

			if (!cancelHandler)
				return;

			RETHREAD_ANNOTATE_AFTER(std::addressof(_cancel_handler));
			RETHREAD_ANNOTATE_FORGET(std::addressof(_cancel_handler));
			{
				// We have to unlock this mutex because cancel by itself may lock some mutexes, thus leading to deadlock
				detail::reverse_lock<std::unique_lock<std::mutex>> ul(l);
				cancelHandler->cancel();
			}
			RETHREAD_ANNOTATE_BEFORE(cancelHandler);
		}

		friend class cancellation_token_source;
	};


	class cancellation_token_source
	{
		using data = detail::cancellation_source_data<sourced_cancellation_token>;
		using data_ptr = std::shared_ptr<data>;

		data_ptr _data;

	public:
		cancellation_token_source() : _data(std::make_shared<data>())
		{ }

		cancellation_token_source(const cancellation_token_source&) = delete;
		cancellation_token_source& operator =(const cancellation_token_source&) = delete;

		~cancellation_token_source()
		{ cancel(); }

		void cancel()
		{
			std::unique_lock<std::mutex> l(_data->_mutex);
			if (_data->_cancelled)
				return;

			_data->_cancelled = true;
			l.unlock();

			{
				std::unique_lock<std::mutex> l(_data->_tokens_mutex);
				for (sourced_cancellation_token& token : _data->_tokens)
					token.cancel_impl(l);
			}

			l.lock();
			_data->_cancel_done = true;
			_data->_cv.notify_all();
		}

		void reset()
		{ _data = std::make_shared<data>(); }

		sourced_cancellation_token create_token()
		{ return sourced_cancellation_token(_data); }
	};


	class cancellation_guard_base
	{
	protected:
		bool try_register(const cancellation_token& token, cancellation_handler& handler)
		{ return token.try_register_cancellation_handler(handler); }

		bool try_unregister(const cancellation_token& token, cancellation_handler& handler)
		{ return token.try_unregister_cancellation_handler(handler); }

		void unregister(const cancellation_token& token, cancellation_handler& handler)
		{ token.unregister_cancellation_handler(handler); }
	};


	class cancellation_guard : public cancellation_guard_base
	{
	private:
		const cancellation_token* _token;
		cancellation_handler*     _handler;

	public:
		cancellation_guard(const cancellation_guard&) = delete;
		cancellation_guard& operator =(const cancellation_guard&) = delete;

		cancellation_guard() :
			_token(nullptr), _handler(nullptr)
		{ }

		cancellation_guard(const cancellation_token& token, cancellation_handler& handler) :
			_token(nullptr), _handler(&handler)
		{
			if (try_register(token, handler))
				_token = &token;
		}

		cancellation_guard(cancellation_guard&& other) :
			_token(other._token), _handler(other._handler)
		{ other._token = nullptr; }

		~cancellation_guard()
		{
			if (!_token || RETHREAD_LIKELY(try_unregister(*_token, *_handler)))
				return;
			unregister(*_token, *_handler);
		}

		bool is_cancelled() const
		{ return !_token; }
	};
}

#endif
