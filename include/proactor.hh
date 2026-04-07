#pragma once

#include "reactor.hh"
#include "util.hh"

namespace Asio {
class ConnectedSocket : MoveOnly {
public:
  ConnectedSocket() = default;
  ConnectedSocket(IOContext &context, IOContext::Handle handle)
      : ctx_{&context}, handle_{handle} {}
  void read(char *buffer, int bufferSize, Callback<int>);
  void write(const char *buffer, int bufferSize, Callback<int>);

private:
  IOContext *ctx_;
  IOContext::Handle handle_;
};

class ListeningSocket : MoveOnly {
public:
  ListeningSocket(IOContext &context, IOContext::Handle handle)
      : ctx_{&context}, handle_{handle} {}
  void accept(sockaddr *, socklen_t *, Callback<ConnectedSocket>);

private:
  IOContext *ctx_;
  IOContext::Handle handle_;
};
} // namespace Asio
