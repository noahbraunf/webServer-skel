#include "socket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace wnet {
FileDescriptor::~FileDescriptor() {
  if (is_valid())
    ::close(_fd);
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept
    : _fd(other._fd) {
  other._fd = -1;
}

FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept {
  if (this != &other) {
    if (is_valid()) {
      ::close(_fd);
    }
    _fd = other._fd;
    other._fd = -1;
  }
  return *this;
}

constexpr void FileDescriptor::reset(int fd) {
  if (is_valid()) {
    ::close(_fd);
  }
  _fd = fd;
}

struct SocketAddr::Impl {
  sockaddr_in addr{};

  Impl() {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // ipv4
  }
};

SocketAddr::SocketAddr() noexcept
    : _impl(std::make_unique<SocketAddr::Impl>()) {}

SocketAddr::SocketAddr(std::string_view ip, uint16_t port)
    : _impl(std::make_unique<SocketAddr::Impl>()) {
  _impl->addr.sin_port =
      htons(port); // must convert endianess to network endianess
  if (inet_pton(AF_INET, ip.data(), &_impl->addr.sin_addr) != 1) {
    throw std::invalid_argument(
        std::format("Invalid IPv4 address supplied: {}", ip));
  }
}

SocketAddr::SocketAddr(const SocketAddr &other)
    : _impl(other._impl ? std::make_unique<Impl>(*other._impl) : nullptr) {}

SocketAddr::SocketAddr(SocketAddr &&other) noexcept
    : _impl(std::move(other._impl)) {}

SocketAddr &SocketAddr::operator=(const SocketAddr &other) {
  if (this != &other) {
    if (other._impl) {
      _impl = std::make_unique<Impl>(*other._impl);
    } else {
      _impl.reset();
    }
  }
  return *this;
}

SocketAddr &SocketAddr::operator=(SocketAddr &&other) noexcept {
  if (this != &other) {
    _impl = std::move(other._impl);
  }

  return *this;
}

SocketAddr::~SocketAddr() noexcept {}

const sockaddr *SocketAddr::asCType() const noexcept {
  return reinterpret_cast<const sockaddr *>(&_impl->addr);
}

sockaddr *SocketAddr::asCType() noexcept {
  return reinterpret_cast<sockaddr *>(&_impl->addr);
}

std::size_t SocketAddr::size() const noexcept { return sizeof(_impl->addr); }

uint16_t SocketAddr::port() const noexcept {
  return ntohs(_impl->addr.sin_port); // convert to host endianess
}

std::string SocketAddr::ip() const {
  std::array<char, INET_ADDRSTRLEN> buf;
  if (inet_ntop(AF_INET, &_impl->addr.sin_addr, buf.data(), sizeof(buf)) ==
      NULL) {
    // EAFNOSUPPORT cannot happen because AF_INET is always supported
    throw new std::length_error(
        std::format("Buffer is too small (something is really wrong because "
                    "INET_ADDRSTRLEN is incorrect). INET_ADDRSTRLEN: {}",
                    INET_ADDRSTRLEN));
  }
  return std::string(buf.data(), buf.size());
}

uint32_t SocketAddr::ipValue() const noexcept {
  return ntohl(_impl->addr.sin_addr.s_addr);
}

bool SocketAddr::operator==(const SocketAddr &other) const noexcept {
  return _impl->addr.sin_family == other._impl->addr.sin_family &&
         _impl->addr.sin_port == _impl->addr.sin_port &&
         _impl->addr.sin_addr.s_addr == other._impl->addr.sin_addr.s_addr;
}

std::string SocketAddr::to_string() const {
  return std::format("{}:{}", ip(), port());
}

struct Socket::Impl {
  FileDescriptor fd;
  Type type{Type::TCP};
  State state; // TODO: figure out starting state, probably closed or
               // uninitialized
  mutable error_code last_error;

  explicit Impl(Type t) : type(t) {}
  [[nodiscard]] bool setOption(int level, int option_name,
                               const void *option_value,
                               socklen_t option_length) {
    if (setsockopt(fd.get(), level, option_name, option_value, option_length) !=
        0) { // error occured if not zero
      last_error = std::error_code(errno, std::system_category());
      return false;
    }
    return true;
  }

  template <typename T>
  [[nodiscard]] bool setOption(int level, int option_name, const T &value) {
    return setOption(level, option_name, &value, sizeof(value));
  }
};

Socket::Socket(Socket::Type type) : _impl(std::make_unique<Impl>(type)) {
  int fd = socket(AF_INET, SOCK_STREAM, 0); // only TCP supported for now
  if (fd < 0) {
    if (_impl->last_error.has_value()) {
      _impl->last_error = std::error_code(errno, std::system_category());
      throw std::runtime_error(std::format("Unable to create socket: {}",
                                           _impl->last_error.value().what()));
    } else {
      throw std::runtime_error(
          "Unable to create socket: no error message given");
    }
  }

  _impl->fd.reset(fd);
}

Socket::~Socket() noexcept {
  if (_impl && _impl->fd.is_valid()) {
    ::close(_impl->fd.get());
  }
}

std::optional<Socket> Socket::create(Socket::Type type) {
  try {
    return std::optional<Socket>{type};
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Socket> Socket::create_bind(const SocketAddr &addr,
                                          Socket::Type type) {
  auto s = create(type);
  if (!s)
    return std::nullopt;
  if (!s->bind(addr))
    return std::nullopt;

  return s;
}

bool Socket::bind(const SocketAddr &addr) {
  if (::bind(_impl->fd.get(), addr.asCType(), addr.size()) != 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }
  _impl->state = State::Bind;
  return true;
}

bool Socket::listen(int backlog) {
  if (_impl->type != Type::TCP) { // connection must be TCP
    _impl->last_error = std::error_code(EOPNOTSUPP, std::system_category());
    return false;
  }

  if (::listen(_impl->fd.get(), backlog) != 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }

  _impl->state = State::Listen;
  return true; // success
}

std::optional<std::pair<Socket, SocketAddr>> Socket::accept() {
  if (_impl->state != State::Listen) {
    _impl->last_error = std::error_code(EINVAL, std::system_category());
    return std::nullopt;
  }

  sockaddr_in client_addr{};
  socklen_t client_len = sizeof(client_addr);

  int cfd = ::accept(_impl->fd.get(),
                     reinterpret_cast<sockaddr *>(&client_addr), &client_len);

  if (cfd < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  Socket client(_impl->type);
  client._impl->fd.reset(cfd);
  client._impl->state = State::Connect;

  SocketAddr addr;
  addr._impl->addr = client_addr;

  auto pair =
      std::make_pair<Socket, SocketAddr>(std::move(client), std::move(addr));
  return pair;
}

bool Socket::connect(const SocketAddr &addr) {
  int result = ::connect(_impl->fd.get(), addr.asCType(), addr.size());

  if (result < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return false;
  }

  _impl->state = State::Connect;
  return true;
}

std::optional<std::size_t> Socket::send(std::span<const std::byte> data) {
  ssize_t sent =
      ::send(_impl->fd.get(), data.data(), data.size(), MSG_NOSIGNAL);
  if (sent < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  return static_cast<std::size_t>(sent);
}

std::optional<std::size_t> Socket::send(std::string_view data) {
  return send(
      std::span{reinterpret_cast<const std::byte *>(data.data()), data.size()});
}

std::optional<std::size_t> Socket::recv(std::span<std::byte> buffer) {
  ssize_t received = ::recv(_impl->fd.get(), buffer.data(), buffer.size(), 0);
  if (received < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  return static_cast<std::size_t>(received);
}

std::optional<std::size_t> Socket::recv(std::span<char> buf) {
  return Socket::recv(std::as_writable_bytes(buf));
}

std::optional<std::string> Socket::recv_string(std::size_t max_length) {
  std::string buf(max_length, '\0');
  ssize_t received = ::recv(_impl->fd.get(), buf.data(), max_length, 0);
  if (received < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  buf.resize(received);
  return buf;
}

std::optional<std::string> Socket::recv_line(std::size_t max_length) {
  std::string result;
  result.reserve(max_length);

  for (std::size_t i = 0; i < max_length; ++i) {
    char cur;
    ssize_t received = ::recv(_impl->fd.get(), &cur, 1, 0);
    if (received < 0) {
      _impl->last_error = std::error_code(errno, std::system_category());
      return std::nullopt;
    }

    if (received == 0) { // no more bytes sent
      break;
    }

    result += cur;

    if (cur == '\n') {
      break;
    }
  }

  return result;
}

[[nodiscard]] std::optional<std::string>
Socket::recv_until(std::string_view delim, std::size_t max_length) {
  std::string result;
  result.reserve(max_length);

  for (std::size_t i = 0; i < max_length; ++i) {
    char cur;
    ssize_t received = ::recv(_impl->fd.get(), &cur, 1, 0);
    if (received < 0) {
      _impl->last_error = std::error_code(errno, std::system_category());
      return std::nullopt;
    }

    if (received == 0) { // no more bytes sent
      break;
    }

    result += cur;

    if (result.size() >= delim.size() &&
        result.substr(result.size() - delim.size()) == delim) {
      break;
    }
  }

  return result;
}

std::optional<std::pair<std::size_t, SocketAddr>>
Socket::recv_from(std::span<std::byte> buf) {
  SocketAddr fromaddr;
  socklen_t fromsize = fromaddr.size();

  ssize_t received = ::recvfrom(_impl->fd.get(), buf.data(), buf.size(), 0,
                                fromaddr.asCType(), &fromsize);

  if (received < 0) {
    _impl->last_error = std::error_code(errno, std::system_category());
    return std::nullopt;
  }

  SocketAddr addr(fromaddr);
  return std::make_pair(static_cast<std::size_t>(received), std::move(addr));
}

void Socket::close() noexcept {
  if (_impl && _impl->fd.is_valid()) {
    ::close(_impl->fd.get());
    _impl->fd.reset(-1);
    _impl->state = State::Close;
  }
}

std::error_code Socket::shutdown(bool read, bool write) {
  if (!_impl || !_impl->fd.is_valid()) {
    return std::error_code(EBADF, std::system_category());
  }

  int how = 0;
  if (read && write) {
    how = SHUT_RDWR;
  } else if (read) {
    how = SHUT_RD;
  } else if (write) {
    how = SHUT_WR;
  } else {
    return std::error_code(EINVAL, std::system_category());
  }

  if (::shutdown(_impl->fd.get(), how) != 0) {
    return std::error_code(errno, std::system_category());
  }

  return std::error_code{};
}

} // namespace wnet
