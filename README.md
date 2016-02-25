rethread
========

rethread library contains implementation of cancellation_token along with RAII-compliant wrapper around C++11 std::thread

[![Build Status](https://travis-ci.org/bo-on-software/rethread.svg?branch=master)](https://travis-ci.org/bo-on-software/rethread)

Rationale
---------
C++11 threads has one inherent problem - they aren't truly RAII-compliant. C++11 standard excerpt:

> 30.3.1.3 thread destructor  
>   ~thread();  
>   If joinable() then **terminate**(), otherwise no effects.  

Let's fix it: meet the **cancellation_token** object.

It is stored inside **rethread::thread** object and passed to invoked function, thus allowing graceful function cancelation from thread destructor. After notifying invoked function about requested cancellation, **rethread::thread** waits for it's completion by joining the underlying thread.

```
class thread
{
  // ...
  
  ~thread() {
    if (joinable())
    {
      token.cancel();
      join();
    }
  }
  
  // ...
};
```

Features
--------

Cancellation token adds cancellability to several blocking calls:
* [this_thread::sleep_for](rethread/thread.hpp#L87)
* [Condition variables](rethread/condition_variable.hpp#L85)
* [POSIX poll](rethread/poll.hpp#L49)

Usage
-----

TODO
