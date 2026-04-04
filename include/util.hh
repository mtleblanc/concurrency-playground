#pragma once

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
} // namespace Asio
