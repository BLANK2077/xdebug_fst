#include "session/uds_transport.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using xdebug_fst::Json;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "UDS transport test failed: " << message << '\n';
        std::exit(1);
    }
}

void wait_for_socket(const std::string& path, pid_t child) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        struct stat info {};
        if (lstat(path.c_str(), &info) == 0) {
            require(S_ISSOCK(info.st_mode), "listener path is not a socket");
            require((info.st_mode & 0777) == 0600, "socket permissions are not 0600");
            return;
        }
        int status = 0;
        const pid_t result = waitpid(child, &status, WNOHANG);
        require(result == 0, "server exited before publishing the socket");
        usleep(10000);
    }
    require(false, "timed out waiting for listener socket");
}

void serve_two_requests(const std::string& path) {
    std::string error;
    const int listener = xdebug_fst::create_uds_listener(path, error);
    if (listener < 0) _exit(10);

    Json request;
    int client = -1;
    if (!xdebug_fst::uds_receive_request(listener, request, client, error)) {
        close(listener);
        _exit(11);
    }
    const Json response{{"ok", true}, {"echo", request}};
    if (!xdebug_fst::uds_send_response(client, response, error)) {
        close(listener);
        _exit(12);
    }

    // A malformed frame must be rejected without poisoning the listener.
    if (xdebug_fst::uds_receive_request(listener, request, client, error) ||
        client != -1 || error.find("invalid UDS JSON line") == std::string::npos) {
        close(listener);
        _exit(13);
    }
    close(listener);
    unlink(path.c_str());
    _exit(0);
}

}  // namespace

int main() {
    char directory[] = "/tmp/xdebug-fst-uds-XXXXXX";
    char* root = mkdtemp(directory);
    require(root != nullptr, "mkdtemp failed");
    const std::string socket_path = std::string(root) + "/engine.sock";

    const pid_t child = fork();
    require(child >= 0, "fork failed");
    if (child == 0) serve_two_requests(socket_path);

    wait_for_socket(socket_path, child);
    const Json request{{"action", "server.ping"}, {"nonce", "round-trip"}};
    Json response;
    std::string error;
    require(!xdebug_fst::uds_request(socket_path, request, response, 0, error) &&
                error == "UDS timeout must be positive",
            "non-positive timeout was not rejected before connect");
    require(xdebug_fst::uds_request(socket_path, request, response, 1000, error),
            "round-trip failed: " + error);
    require(response == Json{{"ok", true}, {"echo", request}},
            "round-trip response differs");

    const int malformed = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    require(malformed >= 0, "malformed client socket failed");
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    require(connect(malformed, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
            "malformed client connect failed");
    const char payload[] = "{not-json}\n";
    require(write(malformed, payload, sizeof(payload) - 1) ==
                static_cast<ssize_t>(sizeof(payload) - 1),
            "malformed client write failed");
    close(malformed);

    int status = 0;
    require(waitpid(child, &status, 0) == child, "waitpid failed");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "server child failed with status " + std::to_string(status));
    require(rmdir(root) == 0, "temporary directory cleanup failed");
    return 0;
}
