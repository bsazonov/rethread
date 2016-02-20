#ifndef RETHREAD_CONDITION_VARIABLE_H
#define RETHREAD_CONDITION_VARIABLE_H

#include <rethread/detail/utility.hpp>
#include <rethread/cancellation_token.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rethread
{
	namespace detail
	{
		class cv_cancellation_handler : public cancellation_handler
		{
			std::condition_variable&      _cv;
			std::unique_lock<std::mutex>& _lock;

		public:
			cv_cancellation_handler(std::condition_variable& cv, std::unique_lock<std::mutex>& lock) : _cv(cv), _lock(lock)
			{ }

			void cancel() override
			{
				// Canceller thread can get stuck here if waiting already ended.
				// We resolve it by special magic in cv_cancellation_guard::~cv_cancellation_guard().
				std::mutex* m = _lock.mutex();
				RETHREAD_ASSERT(m, "Lock is not associated wit a mutex!");
				std::unique_lock<std::mutex> l(*m);
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
			const cancellation_token& _token;
			cv_cancellation_handler&  _handler;
			bool                      _registered;

		public:
			cv_cancellation_guard(const cv_cancellation_guard&) = delete;
			cv_cancellation_guard& operator = (const cv_cancellation_guard&) = delete;

			cv_cancellation_guard(const cancellation_token& token, cv_cancellation_handler& handler) :
				_token(token), _handler(handler)
			{ _registered = try_register(_token, _handler); }

			~cv_cancellation_guard()
			{
				if (!_registered || RETHREAD_LIKELY(try_unregister(_token)))
					return;

				// We need to unlock mutex before unregistering, because canceller thread
				// can get stuck at mutex in cv_cancellation_handler::cancel().
				// When unregister returns, we are sure that canceller has left cancel(), so it is safe to lock mutex back.
				reverse_lock<std::unique_lock<std::mutex>> ul(_handler.get_lock());
				unregister(_token);
			}

			bool is_cancelled() const
			{ return !_registered; }
		};
	}


	void wait(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(token, handler);
		if (guard.is_cancelled())
			return;
		cv.wait(lock);
	}

	template<class Rep, class Period>
	std::cv_status wait_for(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
		const std::chrono::duration<Rep, Period>& duration, cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(token, handler);
		if (guard.is_cancelled())
			return std::cv_status::no_timeout;
		return cv.wait_for(lock, duration);
	}


	template<class Clock, class Duration>
	std::cv_status wait_until(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
		const std::chrono::time_point<Clock, Duration>& time_point, cancellation_token& token)
	{
		detail::cv_cancellation_handler handler(cv, lock);
		detail::cv_cancellation_guard guard(token, handler);
		if (guard.is_cancelled())
			return std::cv_status::no_timeout;
		return cv.wait_until(lock, time_point);
	}

}

#endif
