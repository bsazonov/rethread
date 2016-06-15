#Rethread rationale
##The problem
C++11 threads has one inherent problem - they aren't truly RAII-compliant. C++11 standard excerpt:

> 30.3.1.3 thread destructor  
>   ~thread();  
>   If joinable() then **terminate()**, otherwise no effects.

What are the consequences? User have to be cautious about destroying threads:

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
  thread_wrapper t([&alive] { while(alive) do_work(); });
  do_work2(); // may throw
  alive = false;
  t.join();
}
```
If `do_work2()` throws, it doesn't cause termination, but it causes thread dtor to hang forever, because `alive` will never become `false`. So, before joining the thread we have to somehow cancel the invoked function. Also, we have to remember that `do_work()` may block on condition_variable or inside OS call.

So the bigger problem is:
####What is the proper way to cancel a long operation, especially if it is waiting on a condition variable or performing a blocking call to the OS?

There are different approaches towards answering this question, such as pthread_cancel, boost::thread::interrupt and generic boolean flag. All of these have their own limitations and flaws, so rethread proposes it's own solution to this question: cancellation token.

##Cancellation token
Typical usage:
```cpp
void do_work(const cancellation_token& token)
{
  std::unique_lock lock(_mutex);
  while (token) // is cancelled?
  {
    if (_tasks.empty())
    {
      rethread::wait(_condition, lock, token); // cancellable wait
      continue;
    }
    auto task = _tasks.front();
    // invoke task
  }
}
```
This example uses two main cancellation_token features:
#####Cancellation state checking
```cpp
while (token)
  \\ ...
```
Converting cancellation_token to boolean is equivalent to the result of `!token.is_cancelled()`. It equals to `true` until token enters cancelled state. If some other thread cancels the token, it will return `false`, thus finishing the loop.
#####Interrupting blocking calls
```cpp
rethread::wait(_condition, lock, token);
```
Cancellation token implements generic way to cancel arbitrary blocking calls. Out of the box rethread provides cancellable implementations for `condition_variable::wait`, `this_thread::sleep`, and UNIX `poll`.

##RAII thread
Using cancellation_token, rethread implements RAII-compliant `std::thread` wrapper: `rethread::thread`. It stores `cancellation_token` object and passes it to the invoked function, thus allowing graceful cancellation from thread destructor. After cancelling the token, `rethread::thread` joins the underlying thread.
```cpp
void use_thread()
{
  rethread::thread t([] (const cancellation_token& token) { while(token) do_work(); });
  do_work2();
}
```
Cancellation token object is passed as the last parameter to thread func, so signature has to be changed appropriately.
