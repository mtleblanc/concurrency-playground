#include <coroutine>

class AsioCoroutine {
public:
  struct Promise {
    AsioCoroutine get_return_object() { return AsioCoroutine{}; }
    void unhandled_exception() noexcept {}
    void return_void() noexcept {}
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
  };
  using promise_type = Promise;
};
