#define BOOST_TEST_MODULE cancellation_token test
#include <boost/test/included/unit_test.hpp>

#include <test/poll.hpp>

#include <rethread/cancellation_token.hpp>
#include <rethread/condition_variable.hpp>
#include <rethread/thread.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

using namespace rethread;

struct test_fixture
{
	std::mutex              _mutex;
	std::condition_variable _cv;
	cancellation_token_impl _token;
	std::atomic<bool>       _finished_flag{false};
	rethread::thread        _thread;

	test_fixture() = default;

	template<class Function>
	test_fixture(Function&& f)
	{ _thread = rethread::thread(std::forward<Function>(f), std::ref(*this)); }
};


BOOST_AUTO_TEST_CASE(cancellation_token_basics_test)
{
	test_fixture f;
	std::thread t([&f] ()
	{
		while (f._token)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		f._finished_flag = true;
	});

	BOOST_REQUIRE(!f._finished_flag);

	f._token.cancel();

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	BOOST_REQUIRE(f._finished_flag);

	t.join();
}


BOOST_AUTO_TEST_CASE(thread_test)
{
	test_fixture f([] (const cancellation_token& t, test_fixture& f)
	{
		while (t)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		f._finished_flag = true;
	});

	BOOST_REQUIRE(!f._finished_flag);
	f._thread.reset();
	BOOST_REQUIRE(f._finished_flag);
}


BOOST_AUTO_TEST_CASE(cv_test)
{
	test_fixture f([] (const cancellation_token&, test_fixture& f)
	{
		std::unique_lock<std::mutex> l(f._mutex);
		while (f._token)
			wait(f._cv, l, f._token);
		f._finished_flag = true;
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	BOOST_REQUIRE(!f._finished_flag);

	f._token.cancel();

	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	BOOST_REQUIRE(f._finished_flag);
}


BOOST_AUTO_TEST_CASE(sleep_test)
{
	test_fixture f([] (const cancellation_token&, test_fixture& f)
	{
		std::unique_lock<std::mutex> l(f._mutex);
		while (f._token)
			rethread::this_thread::sleep_for(std::chrono::minutes(1), f._token);
		f._finished_flag = true;
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	BOOST_REQUIRE(!f._finished_flag);

	f._token.cancel();

	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	BOOST_REQUIRE(f._finished_flag);
}


struct cancellation_delay_tester : cancellation_handler
{
	std::chrono::microseconds _check_delay;
	cancellation_token_impl   _token;
	std::thread               _thread;
	std::atomic<bool>         _alive{true};

	std::atomic<bool>         _cancelled{false};
	std::atomic<bool>         _reset{false};
	std::atomic<bool>         _guard_cancelled{false};

	cancellation_delay_tester(std::chrono::microseconds checkDelay) :
		_check_delay(checkDelay)
	{ _thread = std::thread(std::bind(&cancellation_delay_tester::thread_func, this)); }

	~cancellation_delay_tester()
	{
		BOOST_REQUIRE(!_guard_cancelled);
		BOOST_REQUIRE(!_cancelled);
		BOOST_REQUIRE(!_reset);

		_token.cancel();
		_alive = false;
		_thread.join();

		if (!_guard_cancelled)
		{
			BOOST_REQUIRE(_cancelled);
			BOOST_REQUIRE(_reset);
		}
		else
		{
			BOOST_REQUIRE(!_cancelled);
			BOOST_REQUIRE(!_reset);
		}
	}

	void cancel() override
	{ _cancelled = true; }

	void reset() override
	{ _reset = true; }

	void thread_func()
	{
		std::this_thread::sleep_for(_check_delay);

		cancellation_guard guard(_token, *this);
		_guard_cancelled = guard.is_cancelled();

		while (_alive)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
};


BOOST_AUTO_TEST_CASE(cancellation_delay_test)
{
	const std::chrono::microseconds MaxDelay{100000};
	const std::chrono::microseconds DelayStep{200};
	for (std::chrono::microseconds delay{0}; delay < MaxDelay; delay += DelayStep)
	{
		cancellation_delay_tester t(delay);
		std::this_thread::sleep_for(MaxDelay - delay);
	}
}

