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

#include <rethread/detail/utility.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>


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
		std::atomic<bool>                          _cancelled{false};
		mutable std::atomic<cancellation_handler*> _cancelHandler{nullptr};

	public:
		bool is_cancelled() const
		{ return _cancelled.load(std::memory_order_relaxed); }

		explicit operator bool() const
		{ return !is_cancelled(); }

	protected:
		template<typename Rep, typename Period>
		void sleep_for(const std::chrono::duration<Rep, Period>& duration) const
		{ do_sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(duration)); }

		virtual void do_sleep_for(const std::chrono::nanoseconds& duration) const = 0;

		/// @pre Handler is not registered
		/// @returns Whether handler was registered. If token was cancelled before registration, this method skips registration and returns false
		bool try_register_cancellation_handler(cancellation_handler& handler) const
		{
			cancellation_handler* h = _cancelHandler.exchange(&handler, std::memory_order_release);
			RETHREAD_ANNOTATE_BEFORE(std::addressof(_cancelHandler));
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
			cancellation_handler* h = _cancelHandler.exchange(nullptr, std::memory_order_acquire);
			if (RETHREAD_LIKELY(h == &handler))
				return true;

			RETHREAD_ASSERT(h == HazardPointer(), "Another token was registered!");
			_cancelHandler.exchange(h); // restore value
			return false;
		}

		/// @pre Handler is registered
		/// @post Handler is not registered
		/// @note Will invoke cancellation_handler::reset() if necessary
		virtual void unregister_cancellation_handler(cancellation_handler& handler) const = 0;

	protected:
		cancellation_token() = default;
		cancellation_token(const cancellation_token&) = delete;
		cancellation_token& operator = (const cancellation_token&) = delete;
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
		dummy_cancellation_token& operator = (const dummy_cancellation_token&) = delete;
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


	class cancellation_token_atomic : public cancellation_token
	{
		mutable std::mutex                         _mutex;
		mutable std::condition_variable            _cv;
		mutable bool                               _cancelDone{false};

	public:
		cancellation_token_atomic() = default;
		cancellation_token_atomic(const cancellation_token_atomic&) = delete;
		cancellation_token_atomic& operator = (const cancellation_token_atomic&) = delete;
		~cancellation_token_atomic() = default;

		void cancel()
		{
			cancellation_handler* cancelHandler = nullptr;
			{
				std::unique_lock<std::mutex> l(_mutex);
				if (_cancelled)
					return;

				_cancelled = true;

				cancelHandler = _cancelHandler.exchange(HazardPointer());
				RETHREAD_ASSERT(cancelHandler != HazardPointer(), "_cancelled should protect from double-cancelling");
			}

			if (cancelHandler)
			{
				RETHREAD_ANNOTATE_AFTER(std::addressof(_cancelHandler));
				RETHREAD_ANNOTATE_FORGET(std::addressof(_cancelHandler));
				cancelHandler->cancel();
				RETHREAD_ANNOTATE_BEFORE(cancelHandler);
			}

			{
				std::unique_lock<std::mutex> l(_mutex);
				_cancelDone = true;
				_cv.notify_all();
			}
		}

		void reset()
		{
			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_ASSERT((_cancelHandler.load() || _cancelHandler == HazardPointer()) && (_cancelled == _cancelDone), "Cancellation token is in use!");
			_cancelled = false;
			_cancelDone = false;
		}

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (is_cancelled())
				return;

			_cv.wait_for(l, duration);
		}

		void unregister_cancellation_handler(cancellation_handler& handler) const override
		{
			if (try_unregister_cancellation_handler(handler))
				return;

			std::unique_lock<std::mutex> l(_mutex);
			_cancelHandler = nullptr;

			if (!_cancelled)
				return;

			while (!_cancelDone)
				_cv.wait(l);

			RETHREAD_ANNOTATE_AFTER(std::addressof(handler));
			RETHREAD_ANNOTATE_FORGET(std::addressof(handler));
			l.unlock();
			handler.reset();
		}
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
		cancellation_guard& operator = (const cancellation_guard&) = delete;

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
