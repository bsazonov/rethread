#ifndef RETHREAD_CONDITION_VARIABLE_H
#define RETHREAD_CONDITION_VARIABLE_H


namespace rethread
{
	namespace detail
	{
		class cv_cancellation_handler : public cancellation_handler
		{
			std::unique_lock<std::mutex>& _lock;
			std::condition_variable&      _cv;

		public:
			cv_cancellation_handler(std::unique_lock<std::mutex>& lock, std::condition_variable& cv) : _lock(lock), _cv(cv)
			{ }

			~cv_cancellation_handler()
			{ }

			virtual void cancel()
			{
				std::unique_lock<std::mutex> l(_lock.mutex());
				_cv.notify_all();
			}

			std::unique_lock<std::mutex>& get_lock() const
			{ return _lock; }
		};


		template <typename Lockable_>
		class reverse_lock
		{
			Lockable_& _lockable;

		public:
			reverse_lock(Lockable_& l) : _lockable(l)
			{ _lockable.unlock(); }

			~reverse_lock()
			{ _lockable.lock(); }
		};


		class cv_cancellation_guard : public cancellation_guard_base
		{
			cv_cancellation_handler& _handler;

		public:
			cv_cancellation_guard(ICancellationToken& token, cv_cancellation_handler& handler) :
				cancellation_guard_base(token), _handler(handler)
			{ register(_handler); }

			~cv_cancellation_guard()
			{
				if (try_unregister(_handler))
					return;

				reverse_lock<std::unique_lock<std::mutex> > ul(_handler.get_lock());
				unregister(_handler);
			}
		};
	}


	void wait(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, const cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(handler, token);
		cv.wait(lock);
	}

	template<class Rep, class Period>
	std::cv_status wait_for(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
		const std::chrono::duration<Rep, Period>& duration, cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(handler, token);
		return cv.wait_for(lock, duration);
	}


	template<class Clock, class Duration>
	std::cv_status wait_until(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
		const std::chrono::time_point<Clock, Duration>& time_point, cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(handler, token);
		return cv.wait_until(lock, time_point);
	}

}

#endif
