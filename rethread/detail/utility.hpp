#ifndef RETHREAD_DETAIL_EXCEPTION_HPP
#define RETHREAD_DETAIL_EXCEPTION_HPP

// Copyright (c) 2016, Boris Sazonov
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <exception>
#include <type_traits>

#ifdef __GNUC__
#define RETHREAD_LIKELY(Condition_)   __builtin_expect((Condition_), 1)
#define RETHREAD_UNLIKELY(Condition_) __builtin_expect((Condition_), 0)
#else
#define RETHREAD_LIKELY(Condition_)   (Condition_)
#define RETHREAD_UNLIKELY(Condition_) (Condition_)
#endif

#define RETHREAD_MACRO_BEGIN do {
#if defined (_MSC_VER) && _MSC_VER < 1900
#define RETHREAD_MACRO_END \
        __pragma(warning(push)) \
        __pragma(warning(disable:4127)) \
        } while(false) \
        __pragma(warning(pop))
#else
#define RETHREAD_MACRO_END } while(false)
#endif

#define RETHREAD_THROW(Exception_) ::rethread::detail::throw_exception(Exception_, __FILE__, __LINE__)
#define RETHREAD_CHECK(Condition_, Exception_) RETHREAD_MACRO_BEGIN if (RETHREAD_UNLIKELY(!(Condition_))) RETHREAD_THROW(Exception_); RETHREAD_MACRO_END

#define RETHREAD_FATAL(Message_) RETHREAD_MACRO_BEGIN std::terminate(); RETHREAD_MACRO_END

#ifndef RETHREAD_SUPPRESS_CHECKS
#define RETHREAD_ASSERT(Condition_, Message_) RETHREAD_MACRO_BEGIN if (RETHREAD_UNLIKELY(!(Condition_))) RETHREAD_FATAL(Message_); RETHREAD_MACRO_END
#else
#define RETHREAD_ASSERT(Condition_, Message_) RETHREAD_MACRO_BEGIN RETHREAD_MACRO_END
#endif

#ifdef RETHREAD_USE_HELGRIND_ANNOTATIONS
#include <valgrind/helgrind.h>
#define RETHREAD_ANNOTATE_BEFORE(...) ANNOTATE_HAPPENS_BEFORE(__VA_ARGS__)
#define RETHREAD_ANNOTATE_AFTER(...) ANNOTATE_HAPPENS_AFTER(__VA_ARGS__)
#define RETHREAD_ANNOTATE_FORGET(...) ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(__VA_ARGS__)
#else
#define RETHREAD_ANNOTATE_BEFORE(...)
#define RETHREAD_ANNOTATE_AFTER(...)
#define RETHREAD_ANNOTATE_FORGET(...)
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define RETHREAD_NORETURN __declspec(noreturn)
#else
#define RETHREAD_NORETURN [[noreturn]]
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define RETHREAD_NOEXCEPT
#else
#define RETHREAD_NOEXCEPT noexcept
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define RETHREAD_CONSTEXPR const
#else
#define RETHREAD_CONSTEXPR constexpr
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define RETHREAD_ALIGNOF __alignof
#else
#define RETHREAD_ALIGNOF alignof
#endif

namespace rethread {
namespace detail
{

	template <typename T_>
	RETHREAD_NORETURN inline typename std::enable_if<std::is_base_of<std::exception, T_>::value>::type throw_exception(const T_& t, const char*, int)
	{ throw t; }

}}

#endif
