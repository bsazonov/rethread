#ifndef RETHREAD_DETAIL_EXCEPTION_H
#define RETHREAD_DETAIL_EXCEPTION_H

#define RETHREAD_THROW(Exception_) ::rethread::detail::throw_exception(Exception_, __FILE__, __LINE__)
#define RETHREAD_CHECK(Condition_, Exception_) do { if (!(Condition_)) RETHREAD_THROW(Exception_); } while (false)
#define RETHREAD_LIKELY(Condition_) (Condition_)

namespace rethread {
namespace detail {

	template <typename T>
	[[noreturn]] inline enable_if_t<std::is_base_of<std::exception, T>::value> throw_exception(const T& t, const char* file, int line)
	{ throw t; }

}
}

#endif
