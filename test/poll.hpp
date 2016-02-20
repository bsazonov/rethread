#ifndef TEST_POLL_HPP
#define TEST_POLL_HPP

#include <rethread/cancellation_token.hpp>
#include <rethread/condition_variable.hpp>
#include <rethread/poll.hpp>
#include <rethread/thread.hpp>

#include <boost/scope_exit.hpp>

#include <exception>

BOOST_AUTO_TEST_CASE(poll_test)
{
	using namespace rethread;

	int pipe[2];
	RETHREAD_CHECK(::pipe(pipe) == 0, std::system_error(errno, std::system_category()));

	BOOST_SCOPE_EXIT_ALL(&pipe)
	{ RETHREAD_CHECK(::close(pipe[0]) == 0 || ::close(pipe[1]) == 0, std::system_error(errno, std::system_category())); };

	std::atomic<bool> started{false}, readData{false}, finished{false};

	rethread::thread t([&] (const cancellation_token& token)
	{
		started = true;
		while (token)
		{
			if (rethread::poll(pipe[0], POLLIN, token) != POLLIN)
				continue;

			char dummy = 0;
			RETHREAD_CHECK(::read(pipe[0], &dummy, 1) == 1, std::runtime_error("Can't read data!"));

			readData = true;
		}
		finished = true;
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	BOOST_REQUIRE(started);
	BOOST_REQUIRE(!readData);
	BOOST_REQUIRE(!finished);

	char dummy = 0;
	RETHREAD_CHECK(::write(pipe[1], &dummy, 1) == 1, std::runtime_error("Can't write data!"));
	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	BOOST_REQUIRE(readData);
	BOOST_REQUIRE(!finished);

	t.reset();
	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	BOOST_REQUIRE(finished);
}

#endif
