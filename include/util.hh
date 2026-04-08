#pragma once

#include <expected>
#include <functional>
#include <system_error>
namespace Asio {
class MoveOnly {
protected:
  MoveOnly() = default;
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(const MoveOnly &) = delete;
  MoveOnly &operator=(MoveOnly &&) = default;
  ~MoveOnly() = default;
};

template <typename T> using Result = std::expected<T, std::error_code>;
template <typename T> using Callback = std::function<void(Result<T>)>;
} // namespace Asio
