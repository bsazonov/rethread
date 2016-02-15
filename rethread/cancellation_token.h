#ifndef RETHREAD_CANCELLATION_TOKEN_H
#define RETHREAD_CANCELLATION_TOKEN_H


namespace rethread
{

	namespace this_thread
	{
		template<typename Rep, typename Period>
		inline void sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token);
	}


	struct cancellation_handler
	{
	protected:
		~cancellation_handler() { }

	public:
		/// @brief Should cancel blocking call.
		virtual void cancel() = 0;

		/// @brief Should reset cancellation_handler to it's original state.
		///        For each cancel() call, there's _exactly_ one reset() call.
		virtual void reset() { }
	};


	class cancellation_token
	{
	public:
		virtual void cancel() = 0;
		virtual void reset() = 0;

		virtual bool is_cancelled() const = 0;

		explicit operator bool() const
		{ return !is_cancelled(); }

	protected:
		~cancellation_token() { }

		template<typename Rep, typename Period>
		void sleep_for(const std::chrono::duration<Rep, Period>& duration)
		{ do_sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(duration)); }

		virtual void do_sleep_for(const std::chrono::nanoseconds& duration) = 0;

		/// @returns Whether token was cancelled before handler was registered
		virtual bool try_register_cancellation_handler(cancellation_handler& handler) = 0;

		/// @returns Whether unregistration succeed
		virtual bool try_unregister_cancellation_handler() = 0;

		/// @returns Whether token was cancelled while handler was registered, i.e. whether was handler::cancel() invoked
		virtual bool unregister_cancellation_handler() = 0;

	private:
		friend class cancellation_guard_base;

		template<typename Rep, typename Period>
		friend void this_thread::sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token);
	};


	class dummy_cancellation_token : public cancellation_token
	{
	public:
		virtual void cancel() { }
		virtual void reset()  { }

		virtual bool is_cancelled() const
		{ return false; }

	protected:
		virtual void do_sleep_for(const std::chrono::nanoseconds& duration)           { std::this_thread::sleep_for(duration); }

		virtual bool try_register_cancellation_handler(cancellation_handler& handler) { return false; }
		virtual bool try_unregister_cancellation_handler()                            { return true; }
		virtual bool unregister_cancellation_handler()                                { return false; }
	};


	class cancellation_token_impl : public cancellation_token
	{
		std::mutex              _mutex;
		std::condition_variable _cv;
		std::atomic<bool>       _cancelled;
		bool                    _cancelDone;
		cancellation_handler*   _handler;

	public:
		cancellation_token_impl() : _cancelled(false), _cancelDone(false), _cancelHandler(nullptr)
		{ }

		virtual void cancel()
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

		virtual void reset()
		{
			std::unique_lock<std::mutex> l(_mutex);
			RETHREAD_CHECK(!_cancelHandler && (_cancelled == _cancelDone), std::logic_exception("Cancellation token is in use!"));
			_cancelled = false;
			_cancelDone = false;
		}

		virtual bool is_cancelled() const
		{ return _cancelled.load(std::memory_order_relaxed); }

	protected:
		virtual void do_sleep_for(const std::chrono::nanoseconds& duration)
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return;

			_cond.wait_for(l, duration);
		}

		virtual bool try_register_cancellation_handler(cancellation_handler& handler)
		{
			std::unique_lock<std::mutex> l(_mutex);
			if (_cancelled)
				return true;

			RETHREAD_CHECK(!_cancelHandler, std::logic_error("Cancellation handler already registered!"));
			_cancelHandler = &handler;
			return false;
		}

		virtual bool try_unregister_cancellation_handler()
		{
			std::unique_lock<std::mutex> l(_mutex);
			_cancelHandler = nullptr;

			return !_cancelled;
		}

		virtual bool unregister_cancellation_handler()
		{
			std::unique_lock<std::mutex> l(_mutex);
			_cancelHandler = nullptr;

			if (!_cancelled)
				return false;

			while (!_cancelDone)
				_cv.wait(l);
			return true;
		}
	};


	class cancellation_guard_base
	{
	private:
		cancellation_token&	_token;
		bool                _registered;

	public:
		cancellation_guard_base(const cancellation_guard_base&) = delete;

		bool is_cancelled() const
		{ return !_registered; }

	protected:
		cancellation_guard_base(const cancellation_token& token) :
			_token(token), _registered(false)
		{ }

		~cancellation_guard_base()
		{ RETHREAD_ASSERT(!_registered); }

		void register(cancellation_handler& handler)
		{ _registered = _token.try_register_cancellation_handler(handler); }

		bool try_unregister(cancellation_handler& handler)
		{
			if (!_registered)
				return true;

			_registered = !_token.try_unregister_cancellation_handler();
			return !_registered;
		}

		void unregister(cancellation_handler& handler)
		{
			if (!_registered)
				return;

			if (!_token.unregister_cancellation_handler())
				handler.reset();
			_registered = false;
		}
	};


	class cancellation_guard : public cancellation_guard_base
	{
	private:
		cancellation_handler& _handler;

	public:
		cancellation_guard(cancellation_handler& handler, const cancellation_token& token) :
			cancellation_guard_base(token), _handler(handler)
		{ register(_handler); }

		~cancellation_guard()
		{
			if (RETHREAD_LIKELY(try_unregister(_handler)))
				return;
			unregister(_handler);
		}
	};
}

#endif
