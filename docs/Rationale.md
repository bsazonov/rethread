#Rethread rationale
This rationale assumes that the reader is familiar with [getting started guide](docs/Primer.md).

##The problem
C++11 threads has one inherent problem - they aren't truly RAII-compliant. C++11 standard excerpt:

> 30.3.1.3 thread destructor  
>   ~thread();  
>   If joinable() then **terminate()**, otherwise no effects.

What are the consequences? User has to be cautious about destroying threads:

```cpp
void dangerous_foo()
{
  std::thread t([] { do_work(); });
  do_work2(); // may throw - can cause termination!
  t.join();
}

class dangerous_class
{
  std::thread          t;
  std::shared_ptr<int> p;

  dangerous_class() :
    t([] { do_work(); }),
    p(make_shared<int>(0)) // may throw - can cause termination!
  { }

  ~dangerous_class()
  { t.join(); }
}
```
To fix it, rethread implements RAII-compliant thread wrapper. However, implementation of such a wrapper faces a bigger problem.

##The bigger problem
Simply adding join to the thread dtor doesn't make thread any better:
```cpp
thread_wrapper::~thread_wrapper()
{
  if (joinable())
    join();
}
```
Why? Well, let's consider behavior of such a thread in the following code:
```cpp
void use_thread()
{
  std::atomic<bool> alive{true};
  thread_wrapper t([&alive] { while(alive) do_something(); });
  do_work2(); // may throw
  alive = false;
  t.join();
}
```
If `do_work2()` throws, it doesn't cause termination, but it causes thread dtor to hang forever, because `alive` will never become `false`. So, before joining the thread we have to somehow cancel the invoked function. Also, we have to remember that `do_work()` may block on condition_variable or inside OS call.

So the bigger problem is:
#####What is the proper way to cancel a long operation, especially if it is waiting on a condition variable or performing a blocking call to the OS?

There are different approaches towards answering this question, such as pthread_cancel, boost::thread::interrupt and generic boolean flag.

##Existing solutions
###[pthread_cancel](http://pubs.opengroup.org/onlinepubs/009695399/functions/pthread_cancel.html)
Provides mechanism to send cancellation request to target thread. Then cancellable functions in the thread will throw _special_ exception. This exception unwinds target thread's stack and terminates thread. POSIX has a special [list of cancellable functions](http://pubs.opengroup.org/onlinepubs/009695399/functions/xsh_chap02_09.html) (`read`, `write`, `pthread_cond_wait`, etc.).

Pros:
+ Interrupts condition variables
+ Interrupts OS blocking calls (`read`, `write`, etc.)
+ Very hard to ignore cancellation

Cons:
- Unsupported by C++ standard
- Can break C code (especially third-party libraries), causing resource leakage, missing mutex unlocks, crashes, etc.
- Can't be used in exception-free environment
- Cancellable functions require specific precautions when called from dtor (for example, `close` is a cancellable function, so the user has to disable cancellations before calling it)
- Unportable (e.g. Android & Windows)
- Troubles with condition variables in C++14 (this standard added `noexcept` specifier to `condition_variable::wait`)
- No way to cancel tasks instead of threads

###[boost::thread::interrupt](http://www.boost.org/doc/libs/1_61_0/doc/html/thread/thread_management.html#thread.thread_management.tutorial.interruption)
Two main functions for the `boost::thread` interruption facility are `boost::thread::interrupt()` and `boost::this_thread::interruption_point()`. It uses thread-local storage to store cancellation state and throws an exception when cancelled.

Pros:
+ Portable
+ Interrupts `boost::condition_variable::wait`
+ Can be used exception-free environment

Cons:
- Can't be used with standard condition variables
- Requires additional mutex inside `boost::condition_variable`
- Increases `boost::condition_variable::wait` cost by two mutex locks/unlocks
- Can't interrupt system calls (`read`, `write`, etc.)
- No way to cancel tasks instead of threads
- Slightly violates exception philosophy - destruction of a thread can occur during normal program execution

###Boolean flag
Example:
```cpp
struct boolean_flag_example
{
  std::thread       _thread;
  std::atomic<bool> _alive{true};

  boolean_flag_example()
  { _thread = std::thread([this] { work(); }); }

  ~boolean_flag_example()
  {
    _alive = false;
    _thread.join();
  }

  void work()
  {
    while (_alive)
      do_work();
  }
};
```

Pros:
+ Platform-independent
+ Obvious cancellation points

Cons:
- Code duplication
- Impedes decomposition (no easy and efficient way to implement blocking functions)
- No support for condition variables
- Can't interrupt system calls (`read`, `write`, etc.)
##Cancellation token
All of these approaches have limitations and flaws, so `rethread` proposes it's own solution to this question: cancellation token.

Pros:
+ Platform-independent
+ Low overhead
+ Supports any condition variable with standard interface (e.g. `boost::condition_variable`)
+ Supports custom cancellation handlers
+ Eases code decomposition (see `concurrent_queue` example in the [getting started guide](docs/Primer.md))
+ Fine granularity - can cancel tasks instead of threads
+ Can be used in exception-free environment
+ Obvious cancellation points

Cons:
- Adds extra `const cancellation_token&` parameter to every cancellable function
- Cancellable function should have reasonable value to return upon cancellation - sometimes return value adjusting is required
- Every long or blocking loop should have token checks
- Some functions should have both cancellable and non-cancellable version (or `dummy_cancellation_token` as a default value)

##RAII thread
Using `cancellation_token`, rethread implements RAII-compliant `std::thread` wrapper: `rethread::thread`. It stores `cancellation_token` object and passes it to the invoked function, thus allowing graceful cancellation from thread destructor. After cancelling the token, `rethread::thread` joins the underlying thread.
```cpp
void use_thread()
{
  rethread::thread t([] (const cancellation_token& token) { while(token) do_work(); });
  do_work2();
}
```
Cancellation token object is passed as the last parameter to thread func, so signature has to be changed appropriately. `std::bind` ignores extra parameters, so thread funcs composed using it can be used unchanged.
