#ifndef RETHREAD_POLL_H
#define RETHREAD_POLL_H

#include <rethread/cancellation_token.hpp>
#include <rethread/detail/utility.hpp>

#include <exception>

#include <poll.h>
#include <unistd.h>

#if !defined(RETHREAD_DISABLE_EVENTFD)
#include <sys/eventfd.h>
#endif

namespace rethread
{
	namespace detail
	{
#if !defined(RETHREAD_DISABLE_EVENTFD)
		struct poll_cancellation_handler : public cancellation_handler
		{
		private:
			int _eventfd;

		public:
			poll_cancellation_handler()
			{
				_eventfd = ::eventfd(0, 0);
				RETHREAD_CHECK(_eventfd != -1, std::system_error(errno, std::system_category(), "eventfd failed"));
			}

			virtual ~poll_cancellation_handler()
			{ RETHREAD_CHECK(::close(_eventfd) == 0, std::system_error(errno, std::system_category())); }

			poll_cancellation_handler(const poll_cancellation_handler&) = delete;
			poll_cancellation_handler& operator = (const poll_cancellation_handler&) = delete;

			void cancel() override
			{
				uint64_t val = 1;
				RETHREAD_CHECK(::write(_eventfd, &val, sizeof(val)) == sizeof(val), std::system_error(errno, std::system_category()));
			}

			void reset() override
			{
				uint64_t val;
				RETHREAD_CHECK(::read(_eventfd, &val, sizeof(val)) == sizeof(val), std::system_error(errno, std::system_category()));
			}

			int get_fd() const
			{ return _eventfd; }
		};
#else
		struct poll_cancellation_handler : public cancellation_handler
		{
		private:
			int _pipe[2];

		public:
			poll_cancellation_handler()
			{ RETHREAD_CHECK(::pipe(_pipe) == 0, std::system_error(errno, std::system_category())); }

			~poll_cancellation_handler()
			{
				RETHREAD_CHECK(::close(_pipe[0]) == 0, std::system_error(errno, std::system_category()));
				RETHREAD_CHECK(::close(_pipe[1]) == 0, std::system_error(errno, std::system_category()));
			}

			poll_cancellation_handler(const poll_cancellation_handler&) = delete;
			poll_cancellation_handler& operator = (const poll_cancellation_handler&) = delete;

			void cancel() override
			{
				char dummy = 0;
				RETHREAD_CHECK(::write(_pipe[1], &dummy, 1) == 1, std::system_error(errno, std::system_category()));
			}

			void reset() override
			{
				char dummy;
				RETHREAD_CHECK(::read(_pipe[0], &dummy, 1) == 1, std::system_error(errno, std::system_category()));
			}

			int get_fd() const
			{ return _pipe[0]; }
		};
#endif
	}


	inline short poll(int fd, short events, int timeoutMs, const cancellation_token& token)
	{
		detail::poll_cancellation_handler handler;
		cancellation_guard guard(token, handler);
		if (guard.is_cancelled())
			return 0;

		pollfd fds[2] = { };

		fds[0].fd = fd;
		fds[0].events = events;

		fds[1].fd = handler.get_fd();
		fds[1].events = POLLIN;

		RETHREAD_CHECK(::poll(fds, 2, timeoutMs) != -1, std::system_error(errno, std::system_category()));
		return fds[0].revents;
	}


	inline short poll(int fd, short events, const cancellation_token& token)
	{ return poll(fd, events, -1, token); }


	/// @brief   Cancellable version of POSIX read(...). Uses cancellable poll to implement cancellable waiting.
	/// @returns Zero if cancelled, otherwise read() result
	inline ssize_t read(int fd, void* buf, size_t nbyte, const cancellation_token& token)
	{
		if (poll(fd, POLLIN, token) != POLLIN)
			return 0;
		return ::read(fd, buf, nbyte);
	}
}

#endif
