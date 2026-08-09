#include "session/uds_transport.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace xdebug_fst {
namespace {

bool socket_address(const std::string& path, sockaddr_un& address,
                    std::string& error) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        error = "UDS socket path is empty or exceeds sockaddr_un capacity";
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

bool write_all(int fd, const std::string& text, std::string& error) {
    size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t count = write(fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            error = std::string("UDS write failed: ") + std::strerror(errno);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    return true;
}

bool read_line(int fd, std::string& line, std::string& error) {
    line.clear();
    char buffer[4096];
    while (line.size() <= 16 * 1024 * 1024) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            error = std::string("UDS read failed: ") + std::strerror(errno);
            return false;
        }
        if (count == 0) break;
        line.append(buffer, static_cast<size_t>(count));
        const size_t newline = line.find('\n');
        if (newline != std::string::npos) {
            line.resize(newline);
            return true;
        }
    }
    if (line.size() > 16 * 1024 * 1024) error = "UDS JSON line exceeds 16 MiB";
    else error = "UDS peer closed before newline";
    return false;
}

bool parse_line(int fd, Json& value, std::string& error) {
    std::string line;
    if (!read_line(fd, line, error)) return false;
    try {
        value = Json::parse(line);
    } catch (const std::exception& exception) {
        error = std::string("invalid UDS JSON line: ") + exception.what();
        return false;
    }
    return true;
}

}  // namespace

int create_uds_listener(const std::string& socket_path, std::string& error) {
    sockaddr_un address;
    if (!socket_address(socket_path, address, error)) return -1;
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("cannot create UDS socket: ") + std::strerror(errno);
        return -1;
    }
    unlink(socket_path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        chmod(socket_path.c_str(), 0600) != 0 || listen(fd, 16) != 0) {
        error = std::string("cannot bind/listen UDS socket: ") + std::strerror(errno);
        close(fd);
        unlink(socket_path.c_str());
        return -1;
    }
    return fd;
}

bool uds_receive_request(int listener_fd, Json& request, int& client_fd,
                         std::string& error) {
    do {
        client_fd = accept4(listener_fd, nullptr, nullptr, SOCK_CLOEXEC);
    } while (client_fd < 0 && errno == EINTR);
    if (client_fd < 0) {
        error = std::string("UDS accept failed: ") + std::strerror(errno);
        return false;
    }
    if (!parse_line(client_fd, request, error)) {
        close(client_fd);
        client_fd = -1;
        return false;
    }
    return true;
}

bool uds_send_response(int client_fd, const Json& response, std::string& error) {
    const bool ok = write_all(client_fd, response.dump() + "\n", error);
    const bool closed = close(client_fd) == 0;
    return ok && closed;
}

bool uds_request(const std::string& socket_path, const Json& request,
                 Json& response, int timeout_ms, std::string& error) {
    sockaddr_un address;
    if (!socket_address(socket_path, address, error)) return false;
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("cannot create UDS client: ") + std::strerror(errno);
        return false;
    }
    timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = std::string("UDS connect failed: ") + std::strerror(errno);
        close(fd);
        return false;
    }
    const bool written = write_all(fd, request.dump() + "\n", error);
    if (written) shutdown(fd, SHUT_WR);
    const bool read = written && parse_line(fd, response, error);
    close(fd);
    return read;
}

}  // namespace xdebug_fst
