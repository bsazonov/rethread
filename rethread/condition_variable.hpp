#ifndef RETHREAD_CONDITION_VARIABLE_H
#define RETHREAD_CONDITION_VARIABLE_H

// Copyright (c) 2016, Boris Sazonov
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <rethread/detail/utility.hpp>
#include <rethread/cancellation_token.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rethread
{
	namespace detail
	{
		template<typename Condition, typename Lock>
		class cv_cancellation_handler : public cancellation_handler
		{
		public:
			using lock_type = Lock;

		private:
			Condition& _cv;
			Lock&      _lock;

		public:
			cv_cancellation_handler(Condition& cv, Lock& lock) : _cv(cv), _lock(lock)
			{ }

			cv_cancellation_handler(const cv_cancellation_handler&) = delete;
			cv_cancellation_handler& operator = (const cv_cancellation_handler&) = delete;

			void cancel() override
			{
				// Canceller thread can get stuck here if the waiting already ended.
				// We resolve it by special magic in cv_cancellation_guard::~cv_cancellation_guard().
				auto m = _lock.mutex();
				RETHREAD_ASSERT(m, "Lock is not associated wit a mutex!");
				Lock l(*m);
				_cv.notify_all();
			}

			lock_type& get_lock() const
			{ return _lock; }
		};


		template <typename Lockable_>
		class reverse_lock
		{
			Lockable_& _lockable;

		public:
			reverse_lock(Lockable_& l) : _lockable(l)
			{ _lockable.unlock(); }

			reverse_lock(const reverse_lock&) = delete;
			reverse_lock& operator = (const reverse_lock&) = delete;

			~reverse_lock()
			{ _lockable.lock(); }
		};


		template<typename Handler>
		class cv_cancellation_guard : public cancellation_guard_base
		{
			const cancellation_token& _token;
			Handler&                  _handler;
			bool                      _registered;

		public:
			cv_cancellation_guard(const cv_cancellation_guard&) = delete;
			cv_cancellation_guard& operator = (const cv_cancellation_guard&) = delete;

			cv_cancellation_guard(const cancellation_token& token, Handler& handler) :
				_token(token), _handler(handler)
			{ _registered = try_register(_token, _handler); }

			~cv_cancellation_guard()
			{
				if (!_registered || RETHREAD_LIKELY(try_unregister(_token, _handler)))
					return;

				// We need to unlock mutex before unregistering, because canceller thread
				// can get stuck at mutex in cv_cancellation_handler::cancel().
				// When unregister returns, we are sure that canceller has left cancel(), so it is safe to lock mutex back.
				reverse_lock<typename Handler::lock_type> ul(_handler.get_lock());
				unregister(_token, _handler);
			}

			bool is_cancelled() const
			{ return !_registered; }
		};
	}


	template<typename Condition, typename Lock>
	void wait(Condition& cv, Lock& lock, const cancellation_token& token)
	{
		using handler_type = detail::cv_cancellation_handler<Condition, Lock>;
		handler_type handler(cv, lock);
		detail::cv_cancellation_guard<handler_type> guard(token, handler);
		if (guard.is_cancelled())
			return;
		cv.wait(lock);
	}

	template<typename Condition, typename Lock, typename Rep, typename Period>
	std::cv_status wait_for(Condition& cv, Lock& lock, const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token)
	{
		using handler_type = detail::cv_cancellation_handler<Condition, Lock>;
		handler_type handler(cv, lock);
		detail::cv_cancellation_guard<handler_type> guard(token, handler);
		if (guard.is_cancelled())
			return std::cv_status::no_timeout;
		return cv.wait_for(lock, duration);
	}


	template<typename Condition, typename Lock, class Clock, class Duration>
	std::cv_status wait_until(Condition& cv, Lock& lock, const std::chrono::time_point<Clock, Duration>& time_point, const cancellation_token& token)
	{
		using handler_type = detail::cv_cancellation_handler<Condition, Lock>;
		handler_type handler(cv, lock);
		detail::cv_cancellation_guard<handler_type> guard(token, handler);
		if (guard.is_cancelled())
			return std::cv_status::no_timeout;
		return cv.wait_until(lock, time_point);
	}

}

#endif
