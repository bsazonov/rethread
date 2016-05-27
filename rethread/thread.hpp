#ifndef RETHREAD_THREAD_H
#define RETHREAD_THREAD_H

#include <rethread/cancellation_token.hpp>

#include <thread>

namespace rethread
{

	class thread
	{
	private:
		std::thread                                _impl;
		std::unique_ptr<cancellation_token_atomic> _token;

	public:
		using id = std::thread::id;
		using native_handle_type = std::thread::native_handle_type;

	public:
		thread() noexcept = default;
		thread(const thread&) = delete;

		thread(thread&& other) noexcept :
			_impl(std::move(other._impl)), _token(std::move(other._token))
		{ }

		template<class Function, class... Args>
		explicit thread(Function&& f, Args&&... args) : _token(new cancellation_token_atomic())
		{ _impl = std::thread(std::forward<Function>(f), std::ref(*_token), std::forward<Args>(args)...); }

		~thread()
		{
			if (joinable())
				reset();
		}

		thread& operator = (thread&& other) noexcept
		{
			thread tmp(std::move(other));
			swap(tmp);
			return *this;
		}

		void swap(thread& other) noexcept
		{
			_impl.swap(other._impl);
			_token.swap(other._token);
		}

		bool joinable() const noexcept
		{ return _impl.joinable(); }

		id get_id() const noexcept
		{ return _impl.get_id(); }

		native_handle_type native_handle()
		{ return _impl.native_handle(); }

		static unsigned int hardware_concurrency() noexcept
		{ return std::thread::hardware_concurrency(); }

		void join()
		{ _impl.join(); }

		void reset()
		{
			_token->cancel();
			_impl.join();
			*this = rethread::thread();
		}
	};


namespace this_thread
{
	inline thread::id get_id() noexcept
	{ return std::this_thread::get_id(); }

	inline void yield() noexcept
	{ std::this_thread::yield(); }

	template<typename Rep, typename Period>
	inline void sleep_for(const std::chrono::duration<Rep, Period>& duration)
	{ std::this_thread::sleep_for(duration); }

	template<typename Rep, typename Period>
	inline void sleep_for(const std::chrono::duration<Rep, Period>& duration, const cancellation_token& token)
	{ token.sleep_for(duration); }

	template<typename Clock, typename Duration>
	inline void sleep_until(const std::chrono::time_point<Clock, Duration>& timePoint)
	{ std::this_thread::sleep_until(timePoint); }

	template<typename Clock, typename Duration>
	inline void sleep_until(const std::chrono::time_point<Clock, Duration>& timePoint, const cancellation_token& token)
	{ sleep_for(timePoint - Clock::now(), token); }
}
}

#endif
