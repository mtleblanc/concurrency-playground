#include "sockets_raii.hh"
#include <functional>
#include <memory>
#include <string>
#include <system_error>
namespace Asio {
class ConnectedSocket;
class ListeningSocket;
class IOContext;

class IOContext {
public:
  void run();
};

class ConnectedSocket {
public:
  ConnectedSocket(IOContext &context, Socket socket);
  void read(char *buffer, int bufferSize, std::function<void(int)>);
  void write(char *buffer, int bufferSize, std::function<void(int)>);
};

class ListeningSocket {
public
  ListeningSocket(IOContext &context, Socket socket);
  void accept() {}
};
using ReadFunction = std::function<void(std::error_code, std::string &)>;
using WriteFunction = std::function<void(std::error_code, int)>;
using ReadyFunction = std::function<bool()>;
using AcceptFunction =
    std::function<void(std::error_code, std::shared_ptr<ConnectedSocket>)>;

class ConnectedSocket {
public:
  ConnectedSocket(std::shared_ptr<Monitor> mon, std::shared_ptr<Socket> socket)
      : mon_{mon}, socket_{socket} {}

  void read(ReadFunction f);
  void write(std::string data, WriteFunction f);

private:
  std::shared_ptr<Monitor> mon_;
  std::shared_ptr<Socket> socket_;
};

class ListeningSocket {
public:
  ListeningSocket(Multiplex *mp, std::shared_ptr<Monitor> mon, TcpServer server)
      : mp_{mp}, mon_{mon}, server_{std::move(server)} {}

  void accept(AcceptFunction f);

private:
  Multiplex *mp_;
  std::shared_ptr<Monitor> mon_;
  TcpServer server_;
};

private:
struct Reader;
struct Writer;
struct Acceptor;

Multiplex multiplex_;
} // namespace Asio
