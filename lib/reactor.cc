#include "sockets_raii.hh"
#include <expected>
#include <functional>
#include <optional>
#include <poll.h>
#include <system_error>

namespace Asio {

class MoveOnly {
protected:
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(const MoveOnly &) = delete;
  MoveOnly &operator=(MoveOnly &&) = default;
  ~MoveOnly() = default;
};

template <typename T>
using Callback = std::function<void(std::expected<T, std::error_code>)>;

class IOContext : MoveOnly {
public:
  using ReadyAction = std::function<void(Socket &)>;
  struct Handle {
    int fd;
    int generation;
  };

  void run();
  void readable(Handle, ReadyAction);
  void writable(Handle, ReadyAction);
  std::optional<Socket &> get(Handle);
  Handle watch(Socket);

private:
  struct Slot {
    Socket socket{};
    ReadyAction readable{};
    ReadyAction writeable{};
    bool occupied{};
    int generation{};
    int fdsIndex;
  };
  std::vector<pollfd> pollFds_{};
  std::unordered_map<int, Socket> socketIndices{};
};

class ConnectedSocket : MoveOnly {
public:
  ConnectedSocket(IOContext &context, IOContext::Handle handle);
  void read(char *buffer, int bufferSize, Callback<int>);
  void write(char *buffer, int bufferSize, Callback<int>);
};

class ListeningSocket : MoveOnly {
public:
  ListeningSocket(IOContext &context, IOContext::Handle handle);
  void accept(sockaddr *, socklen_t *, Callback<Socket>);
};
} // namespace Asio
