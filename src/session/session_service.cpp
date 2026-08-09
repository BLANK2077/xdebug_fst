#include "session/session_service.h"

#include "common/sha256.h"
#include "session/session_paths.h"
#include "session/session_registry.h"
#include "session/uds_transport.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace xdebug_fst {
namespace {

using xdebug_engine::SessionInfo;
using xdebug_engine::SessionRegistry;
using xdebug_engine::SessionRegistryResult;
using xdebug_engine::SessionRegistryStatus;
namespace fs = std::filesystem;

Json failure(const std::string& code, const std::string& message,
             const std::string& layer = "session_manager",
             bool recoverable = true) {
    return {{"ok", false},
            {"error", {{"code", code}, {"message", message},
                       {"recoverable", recoverable}, {"error_layer", layer}}}};
}

bool random_hex_256(std::string& value) {
    unsigned char bytes[32] = {};
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t offset = 0;
    while (offset < sizeof(bytes)) {
        const ssize_t count = read(fd, bytes + offset, sizeof(bytes) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            close(fd);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    if (close(fd) != 0) return false;
    static const char hex[] = "0123456789abcdef";
    value.clear();
    value.reserve(64);
    for (unsigned char byte : bytes) {
        value.push_back(hex[byte >> 4]);
        value.push_back(hex[byte & 0x0f]);
    }
    return true;
}

bool canonical_existing_path(const std::string& input, bool directory,
                             std::string& output, std::string& error) {
    std::error_code ec;
    const fs::path path = fs::canonical(input, ec);
    if (ec || (directory ? !fs::is_directory(path, ec)
                         : !fs::is_regular_file(path, ec))) {
        error = std::string(directory ? "directory is unavailable: "
                                      : "file is unavailable: ") + input;
        return false;
    }
    output = path.string();
    return true;
}

bool resolve_design_bundle(const std::string& bundle_input,
                           std::string& canonical_bundle,
                           std::string& library, std::string& error) {
    if (!canonical_existing_path(bundle_input, true, canonical_bundle, error))
        return false;
    const fs::path manifest_path =
        fs::path(canonical_bundle) / "xdebug-design-db.json";
    std::ifstream input(manifest_path);
    if (!input) {
        error = "DesignDB bundle is missing xdebug-design-db.json: " +
                canonical_bundle;
        return false;
    }
    Json manifest;
    try {
        input >> manifest;
    } catch (const std::exception& exception) {
        error = std::string("DesignDB bundle manifest is invalid JSON: ") +
                exception.what();
        return false;
    }
    if (!manifest.is_object() || manifest.size() != 2 ||
        manifest.value("schema_version", std::string()) !=
            "xdebug.design-db-bundle.v1" ||
        !manifest.contains("library") || !manifest["library"].is_string() ||
        manifest["library"].get<std::string>().empty()) {
        error = "DesignDB bundle manifest must contain exactly schema_version="
                "xdebug.design-db-bundle.v1 and a nonempty library";
        return false;
    }
    const fs::path relative(manifest["library"].get<std::string>());
    if (relative.is_absolute()) {
        error = "DesignDB bundle library must be relative to the bundle";
        return false;
    }
    std::error_code ec;
    const fs::path resolved = fs::canonical(fs::path(canonical_bundle) / relative, ec);
    const fs::path root = fs::canonical(canonical_bundle, ec);
    const std::string root_prefix = root.string() + "/";
    if (ec || !fs::is_regular_file(resolved, ec) ||
        resolved.extension() != ".so" ||
        resolved.string().rfind(root_prefix, 0) != 0) {
        error = "DesignDB bundle library must resolve to one in-bundle .so";
        return false;
    }
    library = resolved.string();
    return true;
}

bool populate_fingerprint(const std::string& path, bool design,
                          SessionInfo& session) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) return false;
    if (design) {
        session.dbdir_mtime = info.st_mtime;
        session.dbdir_size = info.st_size;
        session.dbdir_dev = info.st_dev;
        session.dbdir_inode = info.st_ino;
    } else {
        session.fsdb_mtime = info.st_mtime;
        session.fsdb_size = info.st_size;
        session.fsdb_dev = info.st_dev;
        session.fsdb_inode = info.st_ino;
    }
    return true;
}

Json public_session(const SessionInfo& session) {
    const char* mode = session.dbdir_path.empty()
        ? "waveform"
        : (session.fsdb_file.empty() ? "design" : "combined");
    Json value{{"session_id", session.session_id},
               {"mode", mode},
               {"transport", session.transport}};
    if (!session.dbdir_path.empty()) value["daidir"] = session.dbdir_path;
    if (!session.fsdb_file.empty()) value["fsdb"] = session.fsdb_file;
    if (session.transport == "uds") {
        value["socket_path"] = session.socket_path;
        value["server_host"] = session.server_host;
    } else if (session.transport == "tcp") {
        value["host"] = session.host;
        value["bind_host"] = session.bind_host;
        value["port"] = session.port;
        value["server_host"] = session.server_host;
    } else if (session.transport == "file") {
        value["file_dir"] = session.file_dir;
        value["server_host"] = session.server_host;
    }
    if (session.server_pid > 0) value["server_pid"] = session.server_pid;
    if (session.created_at > 0) value["created_at"] = session.created_at;
    if (session.last_active > 0) value["last_active"] = session.last_active;
    return value;
}

bool endpoint_control(const SessionInfo& session, const std::string& action,
                      Json& response, std::string& error) {
    if (session.transport != "uds") {
        error = "transport is not implemented by this endpoint client: " +
                session.transport;
        return false;
    }
    return uds_request(
        session.socket_path,
        {{"api_version", "xdebug.internal.v1"}, {"action", action},
         {"args", Json::object()}},
        response, 1000, error);
}

bool endpoint_is_generation(const SessionInfo& session) {
    Json response;
    std::string error;
    return endpoint_control(session, "server.ping", response, error) &&
           response.value("ok", false) &&
           response.value("data", Json::object()).value(
               "generation", std::string()) == session.generation;
}

bool cleanup_generation(SessionRegistry& registry, const SessionInfo& session) {
    const bool artifacts = xdebug_design::xdebug_design_remove_session_generation(
        session.session_id, session.generation);
    if (!artifacts) return false;
    const SessionRegistryResult removed = registry.remove_if_generation(
        session.session_id, session.generation);
    return removed.ok();
}

std::string self_executable() {
    char path[4096];
    const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(path))) return "";
    path[size] = '\0';
    return path;
}

pid_t spawn_uds_engine(const SessionInfo& session,
                       const std::string& design_library) {
    const std::string executable = self_executable();
    if (executable.empty()) return -1;
    const pid_t child = fork();
    if (child != 0) return child;

    setsid();
    const std::string log_path =
        xdebug_design::xdebug_design_debug_log_path(session.session_id);
    const int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    const int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) dup2(null_fd, STDIN_FILENO);
    if (log_fd >= 0) {
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
    }
    if (null_fd > STDERR_FILENO) close(null_fd);
    if (log_fd > STDERR_FILENO) close(log_fd);

    std::vector<std::string> arguments = {
        executable, "--server", "--session-id", session.session_id,
        "--generation", session.generation,
        "--socket-path", session.socket_path};
    if (!session.fsdb_file.empty()) {
        arguments.push_back("-fst");
        arguments.push_back(session.fsdb_file);
    }
    if (!design_library.empty()) {
        arguments.push_back("-dbdir");
        arguments.push_back(design_library);
    }
    std::vector<char*> argv;
    for (std::string& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    execv(executable.c_str(), argv.data());
    _exit(127);
}

Json open_session(const Json& request) {
    const Json target = request.value("target", Json::object());
    const Json args = request.value("args", Json::object());
    const std::string session_id = args.value("name", std::string());
    const std::string transport = args.value("transport", "uds");
    if (transport != "uds") {
        return failure("TRANSPORT_UNAVAILABLE",
                       "requested transport is not implemented yet: " + transport,
                       "transport");
    }

    SessionInfo session;
    session.session_id = session_id;
    session.lifecycle_state = "opening";
    session.transport = "uds";
    session.socket_path = xdebug_design::xdebug_design_socket_path(session_id);
    session.server_host = "localhost";
    session.created_at = time(nullptr);
    session.last_active = session.created_at;
    if (!random_hex_256(session.generation)) {
        return failure("SESSION_GENERATION_FAILED",
                       "failed to create a cryptographic lifecycle generation",
                       "internal", false);
    }
    const std::string ownership_token =
        args.value("ownership_token", std::string());
    if (!ownership_token.empty()) {
        session.ownership_token_hash =
            xdebug_core::sha256_text(ownership_token);
    }

    std::string design_library;
    std::string error;
    if (target.contains("daidir")) {
        if (!resolve_design_bundle(target["daidir"].get<std::string>(),
                                   session.dbdir_path, design_library, error) ||
            !populate_fingerprint(session.dbdir_path, true, session)) {
            return failure("DESIGN_BUNDLE_INVALID", error);
        }
    }
    if (target.contains("fsdb")) {
        if (!canonical_existing_path(target["fsdb"].get<std::string>(), false,
                                     session.fsdb_file, error) ||
            !populate_fingerprint(session.fsdb_file, false, session)) {
            return failure("WAVEFORM_OPEN_FAILED", error);
        }
    }

    SessionRegistry registry;
    SessionRegistryResult reserved = registry.reserve_opening(session);
    if (!reserved.ok()) {
        return failure(
            reserved.status == SessionRegistryStatus::Conflict
                ? "SESSION_ID_EXISTS" : "SESSION_REGISTRY_FAILED",
            reserved.message);
    }
    if (!xdebug_design::xdebug_design_write_generation_marker(
            session_id, session.generation)) {
        registry.remove_if_generation(session_id, session.generation);
        return failure("SESSION_ARTIFACT_FAILED",
                       "failed to commit the session generation marker");
    }

    const pid_t child = spawn_uds_engine(session, design_library);
    if (child <= 0) {
        cleanup_generation(registry, session);
        return failure("SESSION_START_FAILED", "failed to fork the engine process");
    }
    session.server_pid = child;
    if (!registry.update_opening(session, session.generation).ok()) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        cleanup_generation(registry, session);
        return failure("SESSION_REGISTRY_FAILED",
                       "failed to persist opening process evidence");
    }

    bool ready = false;
    int exit_status = 0;
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (endpoint_is_generation(session)) {
            ready = true;
            break;
        }
        const pid_t exited = waitpid(child, &exit_status, WNOHANG);
        if (exited == child) break;
        usleep(10000);
    }
    if (!ready) {
        if (kill(child, 0) == 0) kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        cleanup_generation(registry, session);
        return failure("SESSION_START_TIMEOUT",
                       "engine did not become ready within 5000 ms");
    }

    session.lifecycle_state = "active";
    session.last_active = time(nullptr);
    if (!registry.finalize_opening(session, session.generation).ok()) {
        Json ignored;
        endpoint_control(session, "server.quit", ignored, error);
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        cleanup_generation(registry, session);
        return failure("SESSION_REGISTRY_FAILED",
                       "failed to finalize the active generation");
    }
    return {{"ok", true}, {"session", public_session(session)},
            {"summary", {{"status", "opened"}}},
            {"data", {{"run_manifest", nullptr}}}};
}

Json list_sessions() {
    SessionRegistry registry;
    std::vector<SessionInfo> sessions;
    const SessionRegistryResult loaded = registry.load_all(sessions);
    if (!loaded.ok()) return failure("SESSION_REGISTRY_FAILED", loaded.message);
    Json visible = Json::array();
    for (const SessionInfo& session : sessions) visible.push_back(public_session(session));
    return {{"ok", true},
            {"summary", {{"session_count", visible.size()},
                         {"expired_removed_count", 0}}},
            {"data", {{"sessions", visible}}}};
}

Json doctor_session(const Json& request) {
    const std::string id = request["target"].value("session_id", std::string());
    SessionRegistry registry;
    SessionInfo session;
    const SessionRegistryResult found = registry.get(id, session);
    if (!found.ok()) return failure("SESSION_NOT_FOUND", found.message);
    if (session.lifecycle_state != "active" || !endpoint_is_generation(session)) {
        return failure("SESSION_UNHEALTHY", "Session server is not healthy");
    }
    registry.touch_if_generation(id, session.generation, time(nullptr));
    return {{"ok", true}, {"session", public_session(session)},
            {"summary", {{"healthy", true}}},
            {"data", {{"message", "Session is healthy"}}}};
}

Json remove_session(const Json& request, bool force) {
    const std::string id = request["target"].value("session_id", std::string());
    SessionRegistry registry;
    SessionInfo session;
    const SessionRegistryResult found = registry.get(id, session);
    if (!found.ok()) return failure("SESSION_NOT_FOUND", found.message);
    if (force) {
        const std::string token = request.value("args", Json::object()).value(
            "ownership_token", std::string());
        if (!token.empty() && (session.ownership_token_hash.empty() ||
            xdebug_core::sha256_text(token) != session.ownership_token_hash)) {
            return failure("SESSION_OWNERSHIP_TOKEN_MISMATCH",
                           "the supplied ownership token does not match this session.open record",
                           "session_manager", false);
        }
    }

    const bool owned_endpoint = endpoint_is_generation(session);
    if (owned_endpoint && !force) {
        Json ignored;
        std::string error;
        endpoint_control(session, "server.quit", ignored, error);
    } else if (owned_endpoint && session.server_pid > 0) {
        kill(session.server_pid, SIGTERM);
    }
    if (force && owned_endpoint && session.server_pid > 0) {
        for (int attempt = 0; attempt < 100 && kill(session.server_pid, 0) == 0;
             ++attempt) usleep(10000);
        if (kill(session.server_pid, 0) == 0) kill(session.server_pid, SIGKILL);
    }
    if (!cleanup_generation(registry, session)) {
        session.lifecycle_state = "cleanup_failed";
        registry.mark_cleanup_failed(session, session.generation);
        return failure("SESSION_CLEANUP_FAILED",
                       "session stopped but generation cleanup failed");
    }
    return {{"ok", true}, {"summary", {{"removed", true}}},
            {"data", {{"removed_session", public_session(session)}}}};
}

Json gc_sessions() {
    SessionRegistry registry;
    std::vector<SessionInfo> sessions;
    const SessionRegistryResult loaded = registry.load_all(sessions);
    if (!loaded.ok()) return failure("SESSION_REGISTRY_FAILED", loaded.message);
    Json kept = Json::array();
    Json removed = Json::array();
    for (const SessionInfo& session : sessions) {
        if (session.lifecycle_state == "active" && endpoint_is_generation(session)) {
            kept.push_back(public_session(session));
            continue;
        }
        if (cleanup_generation(registry, session)) {
            removed.push_back({
                {"removed_session", public_session(session)},
                {"reason", "unhealthy"},
                {"health_evidence", {{"code", "SESSION_UNHEALTHY"},
                                     {"message", "Server process is not running"},
                                     {"health_status", "process_exited"}}}});
        } else {
            kept.push_back(public_session(session));
        }
    }
    return {{"ok", true},
            {"summary", {{"before_count", sessions.size()},
                         {"kept_count", kept.size()},
                         {"removed_count", removed.size()}}},
            {"data", {{"kept_sessions", kept}, {"removed", removed}}}};
}

}  // namespace

bool is_frontend_session_action(const std::string& action) {
    return action == "session.open" || action == "session.list" ||
           action == "session.doctor" || action == "session.close" ||
           action == "session.kill" || action == "session.gc";
}

bool request_targets_managed_session(const Json& request) {
    if (!request.is_object() || !request.contains("target") ||
        !request["target"].is_object()) return false;
    const Json& target = request["target"];
    return target.contains("session_id") && target["session_id"].is_string() &&
           !target["session_id"].get<std::string>().empty();
}

Json handle_frontend_session_action(const Json& request) {
    const std::string action = request.value("action", std::string());
    if (action == "session.open") return open_session(request);
    if (action == "session.list") return list_sessions();
    if (action == "session.doctor") return doctor_session(request);
    if (action == "session.close") return remove_session(request, false);
    if (action == "session.kill") return remove_session(request, true);
    if (action == "session.gc") return gc_sessions();
    return failure("UNKNOWN_ACTION", "unknown session action: " + action);
}

Json forward_to_managed_session(const Json& request) {
    const std::string id = request["target"].value("session_id", std::string());
    SessionRegistry registry;
    SessionInfo session;
    const SessionRegistryResult found = registry.get(id, session);
    if (!found.ok()) return failure("SESSION_NOT_FOUND", found.message);
    if (session.lifecycle_state != "active") {
        return failure("SESSION_UNHEALTHY", "session generation is not active");
    }
    Json response;
    std::string error;
    if (!uds_request(session.socket_path, request, response, 30000, error)) {
        return failure("TRANSPORT_FAILED", error, "transport");
    }
    registry.touch_if_generation(id, session.generation, time(nullptr));
    return response;
}

}  // namespace xdebug_fst
