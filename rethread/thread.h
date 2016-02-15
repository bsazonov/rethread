#ifndef RETHREAD_THREAD_H
#define RETHREAD_THREAD_H

#include <thread>

namespace rethread
{

	class thread
	{
	private:
		std::thread                         _impl;
		std::unique_ptr<cancellation_token> _token;

	public:
		using id = std::thread::id;
		using native_handle_type = std::thread::native_handle_type;

	public:
		thread() noexcept = default;
		thread(const thread&) = delete;

		thread(thread&& other) noexcept
		{ swap(other); }

		template<class Function, class... Args>
		explicit thread(Function&& f, Args&&... args) : _token(new cancellation_token())
		{ _impl = std::thread(std::bind(std::forward<Function>(f), std::cref(*_token), std::forward<Args>(args)...)); }

		~thread()
		{
			if (joinable())
				join();
		}

		thread& operator = (thread&& other)
		{ swap(other); }

		void swap(thread& other)
		{
			_impl.swap(other._impl);
			_token.swap(other._token);
		}

		bool joinable() const
		{ return _impl.joinable(); }

		id get_id() const
		{ return _impl.get_id(); }

		native_handle_type native_handle()
		{ return _impl.native_handle(); }

		static unsigned int hardware_concurrency()
		{ return std::thread::hardware_concurrency(); }

		void join()
		{
			_token->Cancel();
			_impl.join();
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
