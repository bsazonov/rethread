#Performance of rethread
Source code for benchmarks can be obtained here: [rethread_testing](https://github.com/bo-on-software/rethread_testing/tree/master/benchmark).

**Operating system**: Ubuntu 16.04
**CPU**: Intel Core i7-4770S @ 3.10GHz
**Compiler**: g++ 5.3.1

|Operation|CPU time|
|:--------|---:|
|Register and unregister handler in cancellation token|11.6 ns|
|Check cancellation token state|1.6 ns|
|Create `standalone_cancellation_token`|16.5 ns|
|Create `cancellation_token_source`|57 ns|

Registration of a cancellation handler is the most important use case for cancellation tokens, because it gives a good estimation of overhead induced by cancellability.

`rethread` uses atomic operations for two hottest operations - checking state and registering/unregistering handler. Heavier synchronization primitives come into play just for the cancellation.

Sometimes, cancellation is actually _cheaper_ than the usual approach. To avoid eternal blocking some applications use timeouts for their blocking operations. For example, instead of simple `condition_variable::wait` they use loops similar to the following:
```cpp
while (alive)
{
  condition_variable::wait_for(lock, std::chrono::milliseconds(100));
  // ...
}
```
Such a loop will cause thread to wake up every 100 milliseconds just to check alive flag. But this code is suboptimal even if no useless wakeups actually happen! `condition_variable::wait_for` is more expensive than `condition_variable::wait` - it has to obtain the current time, calculate wakeup time, etc.
