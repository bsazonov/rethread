#Performance of rethread
Source code for benchmarks can be obtained here: [rethread_testing](https://github.com/bo-on-software/rethread_testing/tree/master/benchmark).

`rethread` strives to speed up two hottest operations - checking state and registering/unregistering handler. This is done by using atomics instead of mutexes and manual devirtualization. Both virtuality and mutexes are necessary for `cancellation_token`, but their usage is limited to the cancellation process itself.

##Benchmarks
All benchmarks were performed on a laptop with Intel Core i7-3630QM @ 2.4GHz.

###Ubuntu 16.04
| |CPU time, ns|
|:--------|---:|
|Register and unregister handler|15.0|
|Check cancellation token state|1.7|
|Create `standalone_cancellation_token`|21.3|
|Create `cancellation_token_source`|68.4|

###Windows 10
| |CPU time, ns|
|:--------|---:|
|Register and unregister handler|17.0|
|Check cancellation token state|2.8|
|Create `standalone_cancellation_token`|33.0|
|Create `cancellation_token_source`|108|

I ignored some preparations for benchmarking on Windows (stopping services, etc.), so these results are probably not 100% accurate.

##Negative overhead of cancellability
Sometimes, cancellation is actually _cheaper_ than the usual approach. To avoid eternal blocking some applications use timeouts for their blocking operations. For example, instead of simple `condition_variable::wait` they use loops similar to the following:
```cpp
while (alive)
{
  condition_variable::wait_for(lock, std::chrono::milliseconds(100));
  // ...
}
```
Such a loop will cause thread to wake up every 100 milliseconds just to check alive flag. But this code is suboptimal even if no useless wakeups actually happen! `condition_variable::wait_for` is more expensive than `condition_variable::wait` - it has to obtain the current time, calculate wakeup time, etc.

[rethread_testing](https://github.com/bo-on-software/rethread_testing/tree/master/benchmark/) contains two synthetic benchmarks to measure this difference (`old_concurrent_queue` and `cancellable_concurrent_queue`).

| |CPU time, ns|
|:--------|---:|
|Ubuntu 16.04 & g++ 5.3.1 (timeout-based queue)|5913|
|Ubuntu 16.04 & g++ 5.3.1(cancellable queue)|5824|
|Windows 10 & MSVS 2015 (timeout-based queue)|2467|
|Windows 10 & MSVS 2015 (cancellable queue)|1729|

So, on MSVS 2015 cancellable code runs 1.4 times faster than timeout-based one! On Ubuntu 16.04 difference is negligible, but cancellable code is definitely winning.
