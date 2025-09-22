#ifndef SOCKET_H_
#define SOCKET_H_
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <optional>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

// struct sockaddr;
// struct sockaddr_in;
// struct pollfd;

namespace wnet { // wrapper for network utilities
using error_code = std::optional<std::system_error>;

class SocketAddr;
class Socket;

template <typename T>
concept SocketLike = requires(T t) {
  { t.fd() } -> std::convertible_to<int>;
  { t.is_valid() } -> std::convertible_to<bool>;
  { t.state() } -> std::convertible_to<typename T::State>;
};

template <typename T>
concept AddressLike = requires(T t) {
  { t.ip() } -> std::convertible_to<std::string>;
  { t.port() } -> std::convertible_to<uint16_t>;
  { t.to_string() } -> std::convertible_to<std::string>;
};

class FileDescriptor {
public:
  constexpr FileDescriptor() noexcept = default;
  constexpr explicit FileDescriptor(int fd) noexcept : _fd(fd) {}

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept;
  FileDescriptor &operator=(FileDescriptor &&other) noexcept;

  ~FileDescriptor();

  [[nodiscard]] constexpr int get() const noexcept { return _fd; }
  [[nodiscard]] constexpr int is_valid() const noexcept { return _fd >= 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid();
  }

  constexpr void reset(int fd = -1);
  [[nodiscard]] int release() noexcept; // cede ownership of _fd

private:
  int _fd{-1};
};

class SocketAddr {
public:
  static inline const std::string LOCALHOST = "127.0.0.1";

  SocketAddr() noexcept;
  SocketAddr(std::string_view ip, uint16_t port);

  SocketAddr(const SocketAddr &other);
  SocketAddr(SocketAddr &&other) noexcept;

  SocketAddr &operator=(const SocketAddr &other);
  SocketAddr &operator=(SocketAddr &&other) noexcept;

  ~SocketAddr() noexcept;

  [[nodiscard]] const sockaddr *asCType() const noexcept;
  [[nodiscard]] sockaddr *asCType() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] uint16_t port() const noexcept;
  [[nodiscard]] std::string ip() const;
  [[nodiscard]] uint32_t ipValue() const noexcept;

  [[nodiscard]] bool operator==(const SocketAddr &otherAddr) const noexcept;
  [[nodiscard]] bool
  operator<=>(const SocketAddr &otherAddr) const noexcept = default;

  [[nodiscard]] std::string to_string() const;

private:
  struct Impl; // the underlying socket from libc
  std::unique_ptr<Impl> _impl;
  friend class Socket;
};

struct SocketOptions {
  bool reuse_addr = true;
  bool reuse_port = false;
  bool keep_alive = false;
  bool no_delay = false;
  bool blocking = true;
  std::optional<std::chrono::milliseconds> send_timeout;
  std::optional<std::chrono::milliseconds> recv_timeout;
  std::optional<int> send_buffer_size;
  std::optional<int> recv_buffer_size;
};

class Socket {
public:
  enum class Type {
    TCP,
    UDP, // TODO: add udp support (not needed for this lab)
  };

  enum class State { Create, Listen, Bind, Recv, Accept, Connect, Close };

  explicit Socket(Type type = Type::TCP);
  Socket(Socket &&other) noexcept = default;
  Socket &operator=(Socket &&other) noexcept = default;
  ~Socket() noexcept;

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  [[nodiscard]] static std::optional<Socket> create(Type type = Type::TCP);
  [[nodiscard]] static std::optional<Socket> create_bind(const SocketAddr &addr,
                                                         Type type = Type::TCP);
  [[nodiscard]] static std::optional<Socket>
  create_connect(const SocketAddr &addr, Type type = Type::TCP);

  [[nodiscard]] std::error_code set_options(const SocketOptions &options);

  [[nodiscard]] bool bind(const SocketAddr &addr);
  [[nodiscard]] bool listen(int backlog = 128);
  [[nodiscard]] std::optional<std::pair<Socket, SocketAddr>> accept();
  [[nodiscard]] bool connect(const SocketAddr &addr);

  [[nodiscard]] std::optional<std::size_t>
  send(std::span<const std::byte> data);
  [[nodiscard]] std::optional<std::size_t> send(std::string_view data);
  [[nodiscard]] std::optional<std::size_t>
  send_to(std::span<const std::byte> data, const SocketAddr &addr);

  [[nodiscard]] std::optional<std::size_t> recv(std::span<std::byte> buffer);
  [[nodiscard]] std::optional<std::size_t> recv(std::span<char> buffer);
  [[nodiscard]] std::optional<std::string>
  recv_string(std::size_t max_length = 4096);
  [[nodiscard]] std::optional<std::string>
  recv_line(std::size_t max_length = 4096);
  [[nodiscard]] std::optional<std::string>
  recv_until(std::string_view delim, std::size_t max_length = 4096);
  [[nodiscard]] std::optional<std::pair<std::size_t, SocketAddr>>
  recv_from(std::span<std::byte> buffer);

  [[nodiscard]] bool isValid() const noexcept;
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] State state() const noexcept;
  [[nodiscard]] Type type() const noexcept;

  [[nodiscard]] std::optional<SocketAddr> local_addr() const;
  [[nodiscard]] std::optional<SocketAddr> remote_addr() const;

  [[nodiscard]] std::error_code shutdown(bool read = true, bool write = true);
  void close() noexcept;

  [[nodiscard]] error_code last_error() const noexcept;

private:
  struct Impl; // socket underlying implementation (so I can work on both my mac
               // device and ubuntu)
  std::unique_ptr<Impl> _impl;
  Socket(std::unique_ptr<Impl> impl) noexcept;

  [[nodiscard]] bool can_bind() const noexcept;
  [[nodiscard]] bool can_listen() const noexcept;
  [[nodiscard]] bool can_accept() const noexcept;
  [[nodiscard]] bool can_connect() const noexcept;
  [[nodiscard]] bool can_send_recv() const noexcept;
};

class Poll {
public:
  using Callback = std::function<void(int fd, short events)>;

  Poll() = default;
  ~Poll() noexcept = default;

  void add(int fd, short events, Callback callback);

  template <SocketLike T>
  void add(const T &socket, short events, Callback callback) {
    add(socket.fd(), events, callback);
  }

  void modify(int fd, short events);

  template <SocketLike T> void modify(const T &socket, short events) {
    modify(socket.fd(), events);
  }

  void remove(int fd);

  template <SocketLike T> void remove(const T &socket) { remove(socket.fd()); }

  [[nodiscard]] int
  poll(std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

  void process_events();

  [[nodiscard]] std::size_t size() const noexcept { return _fds.size(); }
  [[nodiscard]] bool empty() const noexcept { return _fds.empty(); }
  [[nodiscard]] bool contains(int fd) const noexcept;

private:
  std::vector<pollfd> _fds;
  std::unordered_map<int, Callback> _callbacks;
  std::vector<std::pair<int, short>> _pending_events;
};

} // namespace wnet
#endif
