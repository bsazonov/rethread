#Getting started with rethread
This tutorial is written for rethread without exceptions.
##Writing cancellable functions
There are several steps to add cancellability to the blocking function. Let's imagine that we want to add cancellability to `concurrent_queue::pop`.
```cpp
T concurrent_queue<T>::pop()
{
  std::unique_lock<std::mutex> l(_mutex);
  _condition.wait(l, [&_queue] { return _queue.empty(); });

  T result = std::move(_queue.front());
  _queue.pop();
  return result;
}
```
We use the predicate-based version of `condition_variable::wait`, which is equivalent of the following loop:
```cpp
  while (_queue.empty())
    _condition.wait(l);
```
###Adjust return value
Function must have some reasonable value upon cancellation. In our case we'll use optional<T> template from boost (and hopefully C++17).
```cpp
optional<T> concurrent_queue<T>::pop()
{
  std::unique_lock<std::mutex> l(_mutex);
  _condition.wait(l, [&_queue] { return _queue.empty(); });

  T result = std::move(_queue.front());
  _queue.pop();
  return result;
}
```
###Add cancellation_token parameter
Cancellable functions should accept cancellation_token by const reference. We'll make use of this constness a bit later.
```cpp
optional<T> concurrent_queue<T>::pop(const cancellation_token& token)
{
  std::unique_lock<std::mutex> l(_mutex);
  _condition.wait(l, [&_queue] { return _queue.empty(); });

  T result = std::move(_queue.front());
  _queue.pop();
  return result;
}
```
###Reimplement blocking bits
In our example thread blocks in `condition_variable::wait`. For condition variables with C++11-like interface rethread provides cancellable wait as a free function. IMPORTANT NOTE: cancellable wait with predicate returns `bool`, similarly to predicate-based versions of `wait_for` and `wait_until` in regular `condition_variable`.
```cpp
optional<T> concurrent_queue<T>::pop(const cancellation_token& token)
{
  std::unique_lock<std::mutex> l(_mutex);
  if (!rethread::wait(_condition, l, [&_queue] { return _queue.empty(); }))
    return nullopt;

  T result = std::move(_queue.front());
  _queue.pop();
  return result;
}
```
###Add dummy_cancellation_token if necessary
Sometimes it makes sense to have non-cancellable version of a function. Use cases:
* Adding cancellability to already existing API without changing its users
* Scaffolding
* Testing
* Sometimes it is the only way to satisfy caller invariants

The simplest way to add non-cancellable version of a function is dummy_cancellation_token. This class implements cancellation token that may never enter cancelled state. Its copyable, so you can add dummy_cancellation_token as a default value.
```cpp
optional<T> concurrent_queue<T>::pop(const cancellation_token& token = dummy_cancellation_token())
{
  std::unique_lock<std::mutex> l(_mutex);
  if (!rethread::wait(_condition, l, [&_queue] { return _queue.empty(); }))
    return nullopt;

  T result = std::move(_queue.front());
  _queue.pop();
  return result;
}
```
That's it! Now `concurrent_queue::pop` supports cancellation.
