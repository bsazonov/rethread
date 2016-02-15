#ifndef RETHREAD_POLL_H
#define RETHREAD_POLL_H

#include <rethread/detail/exception.h>

struct poll_cancellation_handler : public cancellation_handler
{
private:
	int _pipe[2];

public:
	poll_cancellation_handler()
	{ RETHREAD_CHECK(::pipe(_pipe) == 0, std::system_exception(errno, std::system_category())); }

	~poll_cancellation_handler()
	{
		::close(_pipe[0]);
		::close(_pipe[1]);
	}

	virtual void cancel()
	{
		char dummy = 0;
		RETHREAD_CHECK(::write(_pipe[1], &dummy, 1) == 1, std::system_exception(errno, std::system_category()));
	}

	virtual void reset()
	{
		char dummy;
		RETHREAD_CHECK(::read(_pipe[0], &dummy, 1) == 1, std::system_exception(errno, std::system_category()));
	}

	int get_fd() const
	{ return _pipe[0]; }
};


short do_poll(int fd, short events, cancellation_token& token)
{
	poll_cancellation_handler handler;
	cancellation_guard registrar(token.register(handler));

	pollfd fds[2] = { };

	pollfd[0].fd = fd;
	pollfd[0].events = events;

	pollfd[1].fd = handler.get_fd();
	pollfd[1].events = POLLIN;

	return poll(fds, 2, -1);
}

#endif
