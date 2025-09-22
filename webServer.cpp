// **************************************************************************************
// * webServer (webServer.cpp)
// * - Implements a very limited subset of HTTP/1.0, use -v to enable verbose
// debugging output.
// * - Port number 1701 is the default, if in use random number is selected.
// *
// * - GET requests are processed, all other metods result in 400.
// *     All header gracefully ignored
// *     Files will only be served from cwd and must have format file\d.html or
// image\d.jpg
// *
// * - Response to a valid get for a legal filename
// *     status line (i.e., response method)
// *     Cotent-Length:
// *     Content-Type:
// *     \r\n
// *     requested file.
// *
// * - Response to a GET that contains a filename that does not exist or is not
// allowed
// *     statu line w/code 404 (not found)
// *
// * - CSCI 471 - All other requests return 400
// * - CSCI 598 - HEAD and POST must also be processed.
// *
// * - Program is terminated with SIGINT (ctrl-C)
// **************************************************************************************
#include "webServer.h"
#include "logging.h"
#include "socket.h"
#include <array>
#include <atomic>
#include <csignal>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

std::atomic_bool shutdown_requested{false};
// **************************************************************************************
// * Signal Handler.
// * - Display the signal and exit (returning 0 to OS indicating normal
// shutdown)
// * - Optional for 471, required for 598
// **************************************************************************************
void sig_handler(int signo) {
  INFO << std::format("Received signal {}, shutting down...", signo) << ENDL;
  shutdown_requested.store(true);
}

enum class HttpRequestType {
  GET,
  HEAD,
  POST,
  INVALID,
};

struct HttpRequest {
  HttpRequestType method{HttpRequestType::INVALID};
  std::string path;
  std::string http_version;
  std::unordered_map<std::string, std::string> headers;
};

std::string_view get_content_type(std::string_view filename) {
  if (filename.ends_with(".html")) {
    return "text/html";
  } else if (filename.ends_with(".jpg") || filename.ends_with(".jpeg")) {
    return "image/jpeg";
  } else {
    return "application/octet-stream";
  }
}

bool is_valid_filename(std::string_view filename) {
  static const std::regex valid{R"(^/(file[0-9]\.html|image[0-9]\.jpg)$)"};
  return std::regex_match(filename.begin(), filename.end(), valid);
}

std::optional<std::vector<std::byte>>
read_file(const std::filesystem::path &fp) {
  std::error_code ec;
  if (!std::filesystem::exists(fp, ec) || ec) {
    DEBUGL << std::format("File not found: {}", fp.string()) << ENDL;
  }

  const auto fsize = std::filesystem::file_size(fp, ec);
  if (ec) {
    ERROR << std::format("Cannot ascertain file size for: {}", fp.string())
          << ENDL;
  }
  std::ifstream file(fp, std::ios::binary);
  if (!file) {
    ERROR << std::format("Cannot open file: {}", fp.string()) << ENDL;
    return std::nullopt;
  }

  std::vector<std::byte> content(fsize);
  file.read(reinterpret_cast<char *>(content.data()), fsize);

  if (!file) {
    ERROR << std::format("Cannot read file: {}", fp.string()) << ENDL;
    return std::nullopt;
  }

  return content;
}

HttpRequestType parse_method(std::string_view method) {
  if (method == "GET")
    return HttpRequestType::GET;
  if (method == "HEAD")
    return HttpRequestType::HEAD;
  if (method == "POST")
    return HttpRequestType::POST;
  return HttpRequestType::INVALID;
}

// **************************************************************************************
// * processRequest,
//   - Return HTTP code to be sent back
//   - Set filename if appropriate. Filename syntax is valided but existance
//   is not verified.
// **************************************************************************************
std::optional<HttpRequest> read_header(wnet::Socket &client) {
  auto request_data = client.recv_until("\r\n\r\n");
  if (!request_data) {
    ERROR << "Failed to receive request data" << ENDL;
    return std::nullopt;
  }

  DEBUGL << std::format("Received request data:\n{}", *request_data) << ENDL;

  auto first_newline = request_data->find("\r\n");
  if (first_newline == std::string::npos) {
    ERROR << "CRLF required for valid HTTP request." << ENDL;
    return std::nullopt;
  }

  std::string request_line = request_data->substr(0, first_newline);

  std::istringstream sw(request_line);
  std::string method;
  std::string path;
  std::string version;

  if (!(sw >> method >> path >> version)) {
    ERROR << "Unable to parse request" << ENDL;
    return std::nullopt;
  }

  HttpRequest req;
  req.method = parse_method(method);
  req.path = path;
  req.http_version = version;

  INFO << std::format("Successfully parsed request: {} {} {}", method, path,
                      version)
       << ENDL;

  return req;
}

// **************************************************************************
// * Send one line (including the line terminator <LF><CR>)
// * - Assumes the terminator is not included, so it is appended.
// **************************************************************************
void send_line(wnet::Socket &socket, std::string_view line) {
  std::string safeline = std::format("{}\r\n", line);
  auto sent = socket.send(safeline);
  if (!sent) {
    ERROR << "Failed to send line" << ENDL;
  }
  DEBUGL << std::format("Sent: {}", line) << ENDL;
}

// **************************************************************************
// * Send the entire 404 response, header and body.
// **************************************************************************
void send404(wnet::Socket &socket) {
  INFO << "Sending 404 response" << ENDL;
  send_line(socket, "HTTP/1.0 404 Not Found");
  send_line(socket, "Content-Length: 0");
  send_line(socket, "Content-Type text/html");
  send_line(socket, "");
}

// **************************************************************************
// * Send the entire 400 response, header and body.
// **************************************************************************
void send400(wnet::Socket &socket) {
  INFO << "Sending 400 response" << ENDL;
  send_line(socket, "HTTP/1.0 400 Bad Request");
  send_line(socket, "Content-Length: 0");
  send_line(socket, "Content-Type text/html");
  send_line(socket, "");
}

// **************************************************************************************
// * sendFile
// * -- Send a file back to the browser.
// **************************************************************************************
void send_file(wnet::Socket &socket, std::string_view filename,
               bool include_body = true) {
  std::string parsed_fname{filename};
  if (parsed_fname.starts_with('/')) {
    parsed_fname = parsed_fname.substr(1);
  }

  const std::filesystem::path fp = std::filesystem::path{"data"} / parsed_fname;
  INFO << std::format("Attempting to give file: {}", fp.string()) << ENDL;

  auto fcontent = read_file(fp);
  if (!fcontent) {
    send404(socket);
    return;
  }

  const auto content_type = get_content_type(filename);
  const auto content_length = fcontent->size();

  send_line(socket, "HTTP/1.0 200 OK");
  send_line(socket, std::format("Content-Length: {}", content_length));
  send_line(socket, std::format("Content-Type: {}", content_type));
  send_line(socket, "");

  if (include_body && !fcontent->empty()) {
    auto sent = socket.send(std::span{fcontent->data(), fcontent->size()});
    if (!sent) {
      ERROR << "Failed to send file content" << ENDL;
    } else {
      INFO << std::format("Successfully sent {} bytes", *sent) << ENDL;
    }
  }
}

// **************************************************************************************
// * processConnection
// * -- process one connection/request.
// **************************************************************************************

void process_connection(wnet::Socket &client, wnet::SocketAddr &client_addr) {
  // Call readHeader()

  // If read header returned 400, send 400

  // If read header returned 404, call send404

  // 471: If read header returned 200, call sendFile

  // 598 students
  // - If the header was valid and the method was GET, call sendFile()
  // - If the header was valid and the method was HEAD, call a function to send
  // back the header.
  // - If the header was valid and the method was POST, call a function to save
  // the file to dis.
  INFO << std::format("Processing connection from {}", client_addr.to_string())
       << ENDL;

  auto request = read_header(client);
  if (!request) {
    send400(client);
    return;
  }

  if (!is_valid_filename(request->path)) {
    WARNING << std::format("Invalid filename requested: {}", request->path)
            << ENDL;
    send404(client);
    return;
  }

  switch (request->method) {
  case HttpRequestType::GET:
    INFO << std::format("Processing GET request: {}", request->path) << ENDL;
    send_file(client, request->path);
    break;
  case HttpRequestType::HEAD:
    INFO << std::format("Processing HEAD request: {}", request->path) << ENDL;
    send_file(client, request->path, false);
    break;
  case HttpRequestType::POST:
    INFO << "POST method not required" << ENDL;
    send400(client);
    break;
  case HttpRequestType::INVALID:
  default:
    WARNING << "INVALID or unsupported HTTP method" << ENDL;
    send400(client);
    break;
  }
}

std::optional<uint16_t> find_available_port(uint16_t start = 1024,
                                            std::size_t max_attempts = 100) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint16_t> dist(1024, 65535);

  auto test_sock = wnet::Socket::create_bind(
      wnet::SocketAddr{wnet::SocketAddr::LOCALHOST, start});
  if (test_sock) {
    return start;
  }

  for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
    uint16_t port = dist(gen);
    auto test_sock = wnet::Socket::create_bind(
        wnet::SocketAddr{wnet::SocketAddr::LOCALHOST, port});
    if (test_sock) {
      return port;
    }
  }

  return std::nullopt;
}

int main(int argc, char *argv[]) {

  // ********************************************************************
  // * Process the command line arguments
  // ********************************************************************
  int opt;
  while ((opt = getopt(argc, argv, "d:")) != -1) {
    switch (opt) {
    case 'd':
      LOG_LEVEL = std::stoi(optarg);
      break;
    case ':':
    case '?':
    default:
      std::cout << std::format("Usage: {} -d LOG_LEVEL\n", argv[0]);
      return -1;
    }
  }

  // *******************************************************************
  // * Catch all possible signals
  // ********************************************************************
  DEBUGL << "Setting up signal handlers" << ENDL;
  std::signal(SIGINT, sig_handler);
  std::signal(SIGTERM, sig_handler);

  // *******************************************************************
  // * Creating the inital socket using the socket() call.
  // ********************************************************************
  auto port = find_available_port();
  if (!port) {
    FATAL << "could not find available port to start server" << ENDL;
    exit(-1);
  }

  INFO << std::format("Attempting to bind to port: {}", *port) << ENDL;
  auto server_socket = wnet::Socket::create_bind(
      wnet::SocketAddr{wnet::SocketAddr::LOCALHOST, *port});

  if (!server_socket) {
    FATAL << std::format("Failed to create and bind socket on port: {}", *port)
          << ENDL;
    return -1;
  }

  if (!server_socket->listen()) {
    FATAL << "Failed to listen on socket" << ENDL;
    return -1;
  }
  INFO << std::format("Server listening on {}:{}", wnet::SocketAddr::LOCALHOST,
                      *port)
       << ENDL;

  while (!shutdown_requested.load()) {
    DEBUGL << "Waiting for connection" << ENDL;

    auto connection = server_socket->accept();
    if (!connection) {
      if (!shutdown_requested.load()) {
        ERROR << "Failed to accept connection" << ENDL;
      }
      continue;
    }

    auto &[client_socket, client_addr] = *connection;
    DEBUGL << std::format("Accepted connection from: {}",
                          client_addr.to_string())
           << ENDL;
    try {
      process_connection(client_socket, client_addr);
    } catch (const std::exception &e) {
      ERROR << std::format("Failed to process connection: {}", e.what())
            << ENDL;
    }
    client_socket.close();

    DEBUGL << "Connection processed and closed" << ENDL;
  }
  INFO << "Server shutting down gracefully" << ENDL;
  auto _ = server_socket->shutdown();
  return 0;

  // ********************************************************************
  // * The bind() call takes a structure used to spefiy the details of the
  // connection.
  // *
  // * struct sockaddr_in servaddr;
  // *
  // On a cient it contains the address of the server to connect to.
  // On the server it specifies which IP address and port to lisen for
  // connections. If you want to listen for connections on any IP address
  // you use the address INADDR_ANY
  // ********************************************************************

  // ********************************************************************
  // * Binding configures the socket with the parameters we have
  // * specified in the servaddr structure.  This step is implicit in
  // * the connect() call, but must be explicitly listed for servers.
  // *
  // * Don't forget to check to see if bind() fails because the port
  // * you picked is in use, and if the port is in use, pick a different
  // one.
  // ********************************************************************

  // ********************************************************************
  // * Setting the socket to the listening state is the second step
  // * needed to being accepting connections.  This creates a que for
  // * connections and starts the kernel listening for connections.
  // ********************************************************************

  // ********************************************************************
  // * The accept call will sleep, waiting for a connection.  When
  // * a connection request comes in the accept() call creates a NEW
  // * socket with a new fd that will be used for the communication.
  // ********************************************************************
  // int quitProgram = 0;
  // while (!quitProgram) {
  //   int connFd = 0;
  //   DEBUG << "Calling connFd = accept(fd,NULL,NULL)." << ENDL;
  //
  //   DEBUG << "We have recieved a connection on " << connFd
  //         << ". Calling processConnection(" << connFd << ")" << ENDL;
  //   quitProgram = processConnection(connFd);
  //   DEBUG << "processConnection returned " << quitProgram
  //         << " (should always be 0)" << ENDL;
  //   DEBUG << "Closing file descriptor " << connFd << ENDL;
  //   close(connFd);
  // }
  //
  // ERROR << "Program fell through to the end of main. A listening socket may "
  //          "have closed unexpectadly."
  //       << ENDL;
  // closefrom(3);
}
