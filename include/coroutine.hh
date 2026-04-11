#pragma once

#include "proactor.hh"
#include <coroutine>
#include <memory>
#include <utility>

namespace Asio {

class Task {
public:
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename P>
    void await_suspend(std::coroutine_handle<P> handle) noexcept {
      handle.promise().next.resume();
    }
    void await_resume() const noexcept {}
  };

  struct Promise {
    Task get_return_object() {
      return Task{std::coroutine_handle<Promise>::from_promise(*this)};
    }
    void unhandled_exception() noexcept {}
    void return_void() noexcept {}
    std::suspend_never initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }

    std::coroutine_handle<> next;
  };

  struct Awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend([[maybe_unused]] std::coroutine_handle<> calling) {
      handle.promise().next = calling;
    }
    void await_resume() {}

    std::coroutine_handle<Promise> handle;
  };

  Awaiter operator co_await() { return {handle_}; }
  using promise_type = Promise;

  explicit Task(std::coroutine_handle<Promise> handle) : handle_{handle} {}
  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }

private:
  std::coroutine_handle<Promise> handle_;
};

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

template <typename AsyncObject, typename T, typename... Args>
class AsyncAwaitable {
public:
  using Fn = void (AsyncObject::*)(Args..., std::function<void(Result<T>)>);
  AsyncAwaitable(std::shared_ptr<AsyncObject> socket, Fn fn, Args... args)
      : obj_{std::move(socket)}, fn_{fn}, args_{std::forward<Args>(args)...} {}
  auto operator co_await() {
    struct Awaiter {
      AsyncAwaitable &awaitable;
      Result<T> result{};

      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
        std::apply(
            [&](auto &...args) {
              std::invoke(awaitable.fn_, awaitable.obj_, args...,
                          [this, handle](Result<T> res) {
                            result = std::move(res);
                            handle.resume();
                          });
            },
            awaitable.args_);
      }
      Result<T> await_resume() { return std::move(result); }
    };
    return Awaiter{*this};
  }

private:
  std::shared_ptr<AsyncObject> obj_;
  Fn fn_;
  std::tuple<Args...> args_;
};

inline auto coroRead(std::shared_ptr<ConnectedSocket> socket, char *data,
                     int dataSize) {
  return AsyncAwaitable<ConnectedSocket, int, char *, int>{
      socket, &ConnectedSocket::read, data, dataSize};
}

inline auto coroWrite(std::shared_ptr<ConnectedSocket> socket, const char *data,
                      int dataSize) {
  return AsyncAwaitable<ConnectedSocket, int, const char *, int>{
      socket, &ConnectedSocket::write, data, dataSize};
}

inline auto coroAccept(std::shared_ptr<ListeningSocket> socket,
                       sockaddr *addr = nullptr, socklen_t *addrLen = nullptr) {
  return AsyncAwaitable<ListeningSocket, ConnectedSocket, sockaddr *,
                        socklen_t *>{socket, &ListeningSocket::accept, addr,
                                     addrLen};
}
} // namespace Asio
