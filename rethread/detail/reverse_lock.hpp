#ifndef RETHREAD_DETAIL_REVERSE_LOCK_HPP
#define RETHREAD_DETAIL_REVERSE_LOCK_HPP

// Copyright (c) 2016, Boris Sazonov
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

namespace rethread
{
	namespace detail
	{
		template <typename Lockable_>
		class reverse_lock
		{
			Lockable_& _lockable;

		public:
			reverse_lock(Lockable_& l) : _lockable(l)
			{ _lockable.unlock(); }

			reverse_lock(const reverse_lock&) = delete;
			reverse_lock& operator =(const reverse_lock&) = delete;

			~reverse_lock()
			{ _lockable.lock(); }
		};
	}
}

#endif
