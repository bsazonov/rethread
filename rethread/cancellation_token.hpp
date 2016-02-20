#ifndef RETHREAD_CANCELLATION_TOKEN_H
#define RETHREAD_CANCELLATION_TOKEN_H

#include <rethread/detail/utility.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
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
	public:
		virtual void cancel() = 0;
		virtual void reset() = 0;

		virtual bool is_cancelled() const = 0;

		explicit operator bool() const
		{ return !is_cancelled(); }

	protected:
		virtual ~cancellation_token() { }

		template<typename Rep, typename Period>
		void sleep_for(const std::chrono::duration<Rep, Period>& duration) const
		{ do_sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(duration)); }

		virtual void do_sleep_for(const std::chrono::nanoseconds& duration) const = 0;

		/// @pre Handler is not registered
		/// @returns Whether handler was registered. If token was cancelled before registration, this method skips registration and returns false
		virtual bool try_register_cancellation_handler(cancellation_handler& handler) const = 0;

		/// @pre Handler is registered
		/// @returns Whether unregistration succeed
		virtual bool try_unregister_cancellation_handler() const = 0;

		/// @pre Handler is registered
		/// @post Handler is not registered
		/// @note Will invoke cancellation_handle::reset() if necessary
		virtual void unregister_cancellation_handler() const = 0;

	private:
		friend class cancellation_guard_base;

		template<typename Rep, typename Period>
		friend void this_thread::sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token);
	};


	class dummy_cancellation_token : public cancellation_token
	{
	public:
		void cancel() override { }
		void reset() override  { }

		bool is_cancelled() const override
		{ return false; }

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override   { std::this_thread::sleep_for(duration); }

		bool try_register_cancellation_handler(cancellation_handler&) const override { return true; }
		bool try_unregister_cancellation_handler() const override                    { return true; }
		void unregister_cancellation_handler() const override                        { }
	};


	class cancellation_token_impl : public cancellation_token
	{
		mutable std::mutex              _mutex;
		mutable std::condition_variable _cv;
		std::atomic<bool>               _cancelled{false};
		mutable bool                    _cancelDone{false};
		mutable cancellation_handler*   _cancelHandler{nullptr};

	public:
		cancellation_token_impl() = default;
		cancellation_token_impl(const cancellation_token_impl&) = delete;
		cancellation_token_impl& operator = (const cancellation_token_impl&) = delete;

		void cancel() override
		{
			cancellation_handler* cancelHandler = nullptr;
			{
				std::unique_lock<std::mutex> l(_mutex);
				if (_cancelled)
					return;

				cancelHandler = _cancelHandler;
				_cancelled = true;
			}

			if (cancelHandler)
				cancelHandler->cancel();

			{
				std::unique_lock<std::mutex> l(_mutex);
				_cancelDone = true;
				_cv.notify_all();
			}
		}

		void reset() override
		{
			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_ASSERT(!_cancelHandler && (_cancelled == _cancelDone), "Cancellation token is in use!");
			_cancelled = false;
			_cancelDone = false;
		}

		bool is_cancelled() const override
		{ return _cancelled.load(std::memory_order_relaxed); }

	protected:
		void do_sleep_for(const std::chrono::nanoseconds& duration) const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return;

			_cv.wait_for(l, duration);
		}

		bool try_register_cancellation_handler(cancellation_handler& handler) const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return false;

			RETHREAD_CHECK(!_cancelHandler, std::logic_error("Cancellation handler already registered!"));
			_cancelHandler = &handler;
			return true;
		}

		bool try_unregister_cancellation_handler() const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return false;

			_cancelHandler = nullptr;
			return true;
		}

		void unregister_cancellation_handler() const override
		{
			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_ASSERT(_cancelHandler, "No cancellation_handler!");

			cancellation_handler* handler = _cancelHandler;
			_cancelHandler = nullptr;

			if (!_cancelled)
				return;

			while (!_cancelDone)
				_cv.wait(l);

			l.unlock();
			handler->reset();
		}
	};


	class cancellation_guard_base
	{
	protected:
		bool try_register(const cancellation_token& token, cancellation_handler& handler)
		{ return token.try_register_cancellation_handler(handler); }

		bool try_unregister(const cancellation_token& token)
		{ return token.try_unregister_cancellation_handler(); }

		void unregister(const cancellation_token& token)
		{ token.unregister_cancellation_handler(); }
	};


	class cancellation_guard : public cancellation_guard_base
	{
	private:
		const cancellation_token& _token;
		cancellation_handler&     _handler;
		bool                      _registered;

	public:
		cancellation_guard(const cancellation_guard&) = delete;
		cancellation_guard& operator = (const cancellation_guard&) = delete;

		cancellation_guard(const cancellation_token& token, cancellation_handler& handler) :
			_token(token), _handler(handler)
		{ _registered = try_register(_token, _handler); }

		~cancellation_guard()
		{
			if (!_registered || RETHREAD_LIKELY(try_unregister(_token)))
				return;
			unregister(_token);
		}

		bool is_cancelled() const
		{ return !_registered; }
	};
}

#endif
