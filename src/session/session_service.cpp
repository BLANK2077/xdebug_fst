#include "session/session_service.h"

#include "common/env_config.h"
#include "common/sha256.h"
#include "session/session_lifecycle_lease.h"
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
#include <initializer_list>
#include <set>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace xdebug_fst {
namespace {

using xdebug_engine::SessionInfo;
using xdebug_engine::SessionLifecycleLease;
using xdebug_engine::SessionRegistry;
using xdebug_engine::SessionRegistryResult;
using xdebug_engine::SessionRegistryStatus;
namespace fs = std::filesystem;

Json failure(const std::string& code, const std::string& message,
             const std::string& layer = "session_manager",
             bool recoverable = true, const Json& details = Json::object()) {
    Json error{{"code", code}, {"message", message},
               {"recoverable", recoverable}, {"error_layer", layer}};
    if (details.is_object()) {
        for (auto item = details.begin(); item != details.end(); ++item) {
            error[item.key()] = item.value();
        }
    }
    return {{"ok", false}, {"error", std::move(error)}};
}

Json registry_lookup_failure(const SessionRegistryResult& result) {
    return failure(
        result.status == SessionRegistryStatus::NotFound
            ? "SESSION_NOT_FOUND"
            : "SESSION_REGISTRY_FAILED",
        result.message);
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

struct DesignResource {
    std::string path;
    std::string format;
};

bool resolve_design_bundle(const std::string& bundle_input,
                           std::string& canonical_bundle,
                           DesignResource& resource, std::string& error) {
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
    std::string artifact;
    std::string required_extension;
    const std::string schema = manifest.value("schema_version", std::string());
    if (schema == "xdebug.design-db-bundle.v1" && manifest.is_object() &&
        manifest.size() == 2 && manifest.contains("library") &&
        manifest["library"].is_string() &&
        !manifest["library"].get<std::string>().empty()) {
        resource.format = "xdd-so";
        artifact = manifest["library"].get<std::string>();
        required_extension = ".so";
    } else if (schema == "xdebug.design-db-bundle.v2" && manifest.is_object() &&
               manifest.size() == 3 && manifest.value("format", std::string()) ==
                   "binary-v1" && manifest.contains("database") &&
               manifest["database"].is_string() &&
               !manifest["database"].get<std::string>().empty()) {
        resource.format = "binary-v1";
        artifact = manifest["database"].get<std::string>();
        required_extension = ".xddb";
    } else {
        error = "DesignDB bundle manifest must be strict v1 xdd-so or v2 binary-v1";
        return false;
    }
    const fs::path relative(artifact);
    if (relative.is_absolute()) {
        error = "DesignDB bundle artifact must be relative to the bundle";
        return false;
    }
    std::error_code ec;
    const fs::path resolved = fs::canonical(fs::path(canonical_bundle) / relative, ec);
    const fs::path root = fs::canonical(canonical_bundle, ec);
    const std::string root_prefix = root.string() + "/";
    if (ec || !fs::is_regular_file(resolved, ec) ||
        resolved.extension() != required_extension ||
        resolved.string().rfind(root_prefix, 0) != 0) {
        error = "DesignDB bundle artifact must resolve to an in-bundle file of the declared format";
        return false;
    }
    resource.path = resolved.string();
    return true;
}

bool object_has_only(const Json& value,
                     std::initializer_list<const char*> allowed,
                     std::string& unexpected) {
    if (!value.is_object()) return false;
    std::set<std::string> fields;
    for (const char* field : allowed) fields.insert(field);
    for (auto item = value.begin(); item != value.end(); ++item) {
        if (fields.count(item.key()) == 0) {
            unexpected = item.key();
            return false;
        }
    }
    return true;
}

bool lowercase_sha256(const std::string& digest) {
    if (digest.size() != 64) return false;
    for (char character : digest) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) return false;
    }
    return true;
}

bool nonnegative_size(const Json& value, unsigned long long& result) {
    try {
        if (value.is_number_unsigned()) {
            result = value.get<unsigned long long>();
            return true;
        }
        if (!value.is_number_integer()) return false;
        const long long signed_value = value.get<long long>();
        if (signed_value < 0) return false;
        result = static_cast<unsigned long long>(signed_value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Json provenance_failure(const std::string& message, Json evidence) {
    Json details = Json::object();
    for (const char* field : {
             "manifest_path", "resource", "expected_path", "actual_path",
             "expected_size_bytes", "actual_size_bytes", "expected_sha256",
             "actual_sha256"}) {
        if (evidence.contains(field)) details[field] = evidence[field];
    }
    if (evidence.contains("schema_version")) {
        details["manifest_schema_version"] = evidence["schema_version"];
    }
    if (evidence.contains("state")) {
        details["manifest_state"] = evidence["state"];
    }
    return failure("RESOURCE_PROVENANCE_MISMATCH", message,
                   "handler", true, details);
}

bool validate_manifest_resource(const std::string& manifest_path,
                                const std::string& key,
                                const Json& declaration,
                                const std::string& target_path,
                                const Json& common_evidence,
                                Json& error_response) {
    Json evidence = common_evidence;
    evidence["resource"] = key;
    std::string unexpected;
    unsigned long long expected_size = 0;
    if (!object_has_only(declaration, {"path", "size_bytes", "sha256"},
                         unexpected)) {
        error_response = provenance_failure(
            declaration.is_object()
                ? "run manifest resource " + key +
                      " contains unknown field: " + unexpected
                : "run manifest resource " + key +
                      " must be a JSON object",
            evidence);
        return false;
    }
    if (declaration.size() != 3 || !declaration.contains("path") ||
        !declaration["path"].is_string() ||
        declaration["path"].get<std::string>().empty() ||
        fs::path(declaration["path"].get<std::string>()).is_absolute() ||
        !declaration.contains("size_bytes") ||
        !nonnegative_size(declaration["size_bytes"], expected_size) ||
        !declaration.contains("sha256") ||
        !declaration["sha256"].is_string() ||
        !lowercase_sha256(declaration["sha256"].get<std::string>())) {
        error_response = provenance_failure(
            "run manifest resource " + key +
                " must contain exactly relative path, non-negative size_bytes, "
                "and lowercase 64-hex sha256",
            evidence);
        return false;
    }
    const std::string relative = declaration["path"].get<std::string>();
    const std::string expected_sha = declaration["sha256"].get<std::string>();
    evidence["expected_path"] = relative;
    evidence["expected_size_bytes"] = expected_size;
    evidence["expected_sha256"] = expected_sha;
    std::error_code ec;
    const fs::path expected = fs::canonical(
        fs::path(manifest_path).parent_path() / relative, ec);
    if (ec || expected.string() != target_path) {
        evidence["actual_path"] = target_path;
        error_response = provenance_failure(
            "run manifest resource path does not match target: " + key,
            evidence);
        return false;
    }
    evidence["actual_path"] = target_path;
    struct stat info {};
    const int stat_result = stat(target_path.c_str(), &info);
    if (stat_result != 0 || info.st_size < 0 ||
        static_cast<unsigned long long>(info.st_size) != expected_size) {
        evidence["actual_size_bytes"] = stat_result == 0 && info.st_size >= 0
            ? Json(static_cast<unsigned long long>(info.st_size))
            : Json(nullptr);
        error_response = provenance_failure(
            "run manifest resource size does not match target: " + key,
            evidence);
        return false;
    }
    std::string actual_sha;
    std::string sha_error;
    const bool digest_ok = S_ISDIR(info.st_mode)
        ? xdebug_core::sha256_directory_tree(target_path, actual_sha, sha_error)
        : xdebug_core::sha256_file(target_path, actual_sha, sha_error);
    if (!digest_ok || actual_sha != expected_sha) {
        evidence["actual_sha256"] = actual_sha.empty()
            ? Json(nullptr) : Json(actual_sha);
        error_response = provenance_failure(
            "run manifest resource SHA-256 does not match target: " + key,
            evidence);
        return false;
    }
    return true;
}

bool validate_run_manifest(const Json& target, const SessionInfo& session,
                           Json& details, Json& error_response) {
    details = Json::object();
    if (!target.contains("run_manifest")) return true;
    const std::string requested_manifest =
        target["run_manifest"].get<std::string>();
    std::error_code ec;
    const fs::path canonical_manifest = fs::canonical(requested_manifest, ec);
    if (ec) {
        error_response = provenance_failure(
            "run manifest is missing or cannot be resolved",
            {{"manifest_path", requested_manifest}});
        return false;
    }
    const std::string manifest_path = canonical_manifest.string();
    std::ifstream input(manifest_path);
    if (!input.good()) {
        error_response = provenance_failure(
            "run manifest cannot be opened",
            {{"manifest_path", manifest_path}});
        return false;
    }
    Json parsed;
    try {
        input >> parsed;
    } catch (const std::exception&) {
        error_response = provenance_failure(
            "run manifest is not valid JSON",
            {{"manifest_path", manifest_path}});
        return false;
    }
    std::string unexpected;
    if (!parsed.is_object() ||
        !object_has_only(parsed, {"schema_version", "state", "resources"},
                         unexpected)) {
        error_response = provenance_failure(
            parsed.is_object()
                ? "run manifest contains unknown root field: " + unexpected
                : "run manifest root must be a JSON object",
            {{"manifest_path", manifest_path}});
        return false;
    }
    const std::string schema_version =
        parsed.value("schema_version", std::string());
    const std::string state = parsed.value("state", std::string());
    Json evidence{{"manifest_path", manifest_path},
                  {"schema_version", schema_version}, {"state", state}};
    if (schema_version != "xdebug.run-manifest.v1" || state != "published") {
        error_response = provenance_failure(
            "run manifest must be xdebug.run-manifest.v1 in published state",
            evidence);
        return false;
    }
    if (!parsed.contains("resources") || !parsed["resources"].is_object()) {
        error_response = provenance_failure(
            "run manifest resources must be a JSON object",
            evidence);
        return false;
    }
    Json& resources = parsed["resources"];
    if (!object_has_only(resources, {"fsdb", "daidir"}, unexpected) ||
        !resources.contains("fsdb")) {
        error_response = provenance_failure(
            resources.contains("fsdb")
                ? "run manifest resources contains unknown field: " +
                      unexpected
                : "run manifest resources must declare fsdb",
            evidence);
        return false;
    }
    const bool target_has_design = !session.dbdir_path.empty();
    if (resources.contains("daidir") != target_has_design) {
        error_response = provenance_failure(
            "run manifest resources must exactly match target.fsdb and optional target.daidir",
            evidence);
        return false;
    }
    if (!validate_manifest_resource(manifest_path, "fsdb", resources["fsdb"],
                                    session.fsdb_file, evidence, error_response)) {
        return false;
    }
    if (target_has_design &&
        !validate_manifest_resource(manifest_path, "daidir", resources["daidir"],
                                    session.dbdir_path, evidence, error_response)) {
        return false;
    }
    details = {{"schema_version", schema_version}, {"state", state},
               {"resources", resources}, {"manifest_path", manifest_path}};
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

std::string session_mode(const SessionInfo& session) {
    if (!session.dbdir_path.empty() && !session.fsdb_file.empty()) {
        return "combined";
    }
    return session.dbdir_path.empty() ? "waveform" : "design";
}

bool same_resource(const SessionInfo& lhs, const SessionInfo& rhs) {
    const std::string mode = session_mode(lhs);
    if (mode != session_mode(rhs)) return false;
    if (mode == "design") return lhs.dbdir_path == rhs.dbdir_path;
    if (mode == "waveform") return lhs.fsdb_file == rhs.fsdb_file;
    return lhs.dbdir_path == rhs.dbdir_path &&
           lhs.fsdb_file == rhs.fsdb_file;
}

std::string resource_match_kind(const SessionInfo& session) {
    const std::string mode = session_mode(session);
    if (mode == "design") return "same_daidir";
    if (mode == "waveform") return "same_fsdb";
    return "same_combined_resource";
}

Json duplicate_resource_advisories(
    const std::vector<SessionInfo>& before, const SessionInfo& opened) {
    Json advisories = Json::array();
    for (const SessionInfo& existing : before) {
        if (existing.session_id == opened.session_id ||
            !same_resource(existing, opened)) {
            continue;
        }
        advisories.push_back({
            {"code", "RESOURCE_SESSION_ALREADY_ALIVE"},
            {"severity", "info"},
            {"match_kind", resource_match_kind(opened)},
            {"existing_session_id", existing.session_id},
            {"existing_mode", session_mode(existing)},
            {"message", "same resource already has an alive session; "
                        "consider closing one to save resources"},
        });
    }
    return advisories;
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

bool process_is_running(pid_t pid) {
    if (pid <= 0) return false;
    int status = 0;
    const pid_t child = waitpid(pid, &status, WNOHANG);
    if (child == pid) return false;
    if (kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

bool process_matches_generation(const SessionInfo& session) {
    if (!process_is_running(session.server_pid)) return false;
    const std::string path =
        "/proc/" + std::to_string(session.server_pid) + "/cmdline";
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) return false;
    const std::string command((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    return command.find("xdebug-fst") != std::string::npos &&
           command.find(session.session_id) != std::string::npos &&
           command.find(session.generation) != std::string::npos;
}

bool wait_process_exit(pid_t pid, int timeout_ms) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 10) {
        if (!process_is_running(pid)) return true;
        usleep(10000);
    }
    return !process_is_running(pid);
}

struct HealthResult {
    bool healthy = false;
    std::string status;
    std::string message;
};

HealthResult diagnose(const SessionInfo& session) {
    if (session.lifecycle_state != "active") {
        return {false, session.lifecycle_state == "opening"
                           ? "connect_failed" : "process_exited",
                "Session generation is not active"};
    }
    if (!xdebug_design::xdebug_design_generation_matches(
            session.session_id, session.generation)) {
        return {false, "registry_missing",
                "Session registry and generation marker do not match"};
    }
    struct stat info {};
    if (!session.dbdir_path.empty()) {
        if (stat(session.dbdir_path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
            return {false, "dbdir_missing", "Daidir path is missing"};
        }
        if (!xdebug_core::resource_content_matches(
                session.dbdir_mtime, session.dbdir_size,
                info.st_mtime, info.st_size)) {
            return {false, "dbdir_changed",
                    "Daidir metadata changed since session.open"};
        }
    }
    if (!session.fsdb_file.empty()) {
        if (stat(session.fsdb_file.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
            return {false, "fsdb_missing", "Waveform path is missing"};
        }
        if (!xdebug_core::resource_content_matches(
                session.fsdb_mtime, session.fsdb_size,
                info.st_mtime, info.st_size)) {
            return {false, "fsdb_changed",
                    "Waveform metadata changed since session.open"};
        }
    }
    struct stat socket_info {};
    if (lstat(session.socket_path.c_str(), &socket_info) != 0 ||
        !S_ISSOCK(socket_info.st_mode)) {
        return {false, process_is_running(session.server_pid)
                           ? "socket_missing" : "process_exited",
                process_is_running(session.server_pid)
                    ? "Session socket is missing"
                    : "Server process is not running"};
    }
    if (!endpoint_is_generation(session)) {
        return {false, process_is_running(session.server_pid)
                           ? "ping_failed" : "process_exited",
                process_is_running(session.server_pid)
                    ? "Session ping failed or generation differs"
                    : "Server process is not running"};
    }
    return {true, "healthy", "Session is healthy"};
}

bool cleanup_generation(SessionRegistry& registry, const SessionInfo& session) {
    const bool artifacts = xdebug_design::xdebug_design_remove_session_generation(
        session.session_id, session.generation);
    if (!artifacts) return false;
    const SessionRegistryResult removed = registry.remove_if_generation(
        session.session_id, session.generation);
    return removed.ok();
}

bool cleanup_managed_session(SessionRegistry& registry, SessionInfo session,
                             bool force) {
    SessionInfo retained = session;
    retained.lifecycle_state = "cleanup_failed";
    if (!registry.mark_cleanup_failed(retained, session.generation).ok()) {
        return false;
    }

    bool stopped = !process_is_running(session.server_pid);
    const bool endpoint_owned = endpoint_is_generation(session);
    if (!stopped && endpoint_owned && !force) {
        Json ignored;
        std::string error;
        endpoint_control(session, "server.quit", ignored, error);
        stopped = wait_process_exit(session.server_pid, 1500);
    }
    const bool process_owned = endpoint_owned || process_matches_generation(session);
    if (!stopped && process_owned) {
        if (kill(session.server_pid, SIGTERM) == 0 || errno == ESRCH) {
            stopped = wait_process_exit(session.server_pid, 1500);
        }
    }
    if (!stopped && process_owned) {
        if (kill(session.server_pid, SIGKILL) == 0 || errno == ESRCH) {
            stopped = wait_process_exit(session.server_pid, 1500);
        }
    }
    if (!stopped) return false;
    return cleanup_generation(registry, retained);
}

std::string self_executable() {
    char path[4096];
    const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(path))) return "";
    path[size] = '\0';
    return path;
}

pid_t spawn_uds_engine(const SessionInfo& session,
                       const DesignResource& design_resource) {
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
    if (!design_resource.path.empty()) {
        arguments.push_back("-dbdir");
        arguments.push_back(design_resource.path);
        arguments.push_back("--design-db-format");
        arguments.push_back(design_resource.format);
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
    std::string error;
    const int startup_timeout_sec =
        xdebug_core::xdebug_session_start_timeout_sec(error);
    if (startup_timeout_sec < 0) {
        return failure("INVALID_ENVIRONMENT", error,
                       "session_manager", false);
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

    DesignResource design_resource;
    if (target.contains("daidir")) {
        if (!resolve_design_bundle(target["daidir"].get<std::string>(),
                                   session.dbdir_path, design_resource, error) ||
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
    Json manifest_details;
    Json manifest_error;
    if (!validate_run_manifest(
            target, session, manifest_details, manifest_error)) {
        return manifest_error;
    }

    SessionLifecycleLease lease(session_id);
    if (!lease.locked()) {
        return failure(
            "SESSION_LIFECYCLE_LOCK_FAILED",
            "failed to acquire the session lifecycle lease");
    }
    SessionRegistry registry;
    std::vector<SessionInfo> before_sessions;
    const SessionRegistryResult before_loaded =
        registry.load_all(before_sessions);
    if (!before_loaded.ok()) {
        return failure("SESSION_REGISTRY_FAILED", before_loaded.message);
    }
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

    const pid_t child = spawn_uds_engine(session, design_resource);
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
    bool child_exited = false;
    int exit_status = 0;
    const long long startup_attempts =
        static_cast<long long>(startup_timeout_sec) * 100LL;
    for (long long attempt = 0; attempt < startup_attempts; ++attempt) {
        if (endpoint_is_generation(session)) {
            ready = true;
            break;
        }
        const pid_t exited = waitpid(child, &exit_status, WNOHANG);
        if (exited == child) {
            child_exited = true;
            break;
        }
        usleep(10000);
    }
    if (!ready) {
        if (!child_exited && kill(child, 0) == 0) kill(child, SIGKILL);
        if (!child_exited) waitpid(child, &exit_status, 0);
        cleanup_generation(registry, session);
        const int exit_code = WIFEXITED(exit_status)
            ? WEXITSTATUS(exit_status)
            : (WIFSIGNALED(exit_status) ? 128 + WTERMSIG(exit_status) : -1);
        return failure(
            child_exited ? "SESSION_START_FAILED" : "SESSION_START_TIMEOUT",
            child_exited
                ? "engine exited before publishing a healthy endpoint"
                : "engine did not become ready within " +
                      std::to_string(
                          static_cast<long long>(startup_timeout_sec) * 1000LL) +
                      " ms",
            "session_manager", true,
            {{"timeout_ms",
              static_cast<long long>(startup_timeout_sec) * 1000LL},
             {"exit_status", exit_code}});
    }

    SessionInfo current = session;
    if ((!session.dbdir_path.empty() &&
         !populate_fingerprint(session.dbdir_path, true, current)) ||
        (!session.fsdb_file.empty() &&
         !populate_fingerprint(session.fsdb_file, false, current)) ||
        current.dbdir_mtime != session.dbdir_mtime ||
        current.dbdir_size != session.dbdir_size ||
        current.fsdb_mtime != session.fsdb_mtime ||
        current.fsdb_size != session.fsdb_size) {
        Json ignored;
        endpoint_control(session, "server.quit", ignored, error);
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        cleanup_generation(registry, session);
        return failure("SESSION_RESOURCE_CHANGED",
                       "session resource changed while the engine was opening");
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
    Json response{{"ok", true}, {"session", public_session(session)},
                  {"summary", {{"status", "opened"}}},
                  {"data", {{"run_manifest", manifest_details.empty()
                                                 ? Json(nullptr)
                                                 : manifest_details}}}};
    Json advisories =
        duplicate_resource_advisories(before_sessions, session);
    if (!advisories.empty()) response["advisories"] = std::move(advisories);
    return response;
}

Json list_sessions() {
    SessionRegistry registry;
    std::vector<SessionInfo> sessions;
    const SessionRegistryResult loaded = registry.load_all(sessions);
    if (!loaded.ok()) return failure("SESSION_REGISTRY_FAILED", loaded.message);
    std::string timeout_error;
    const int idle_timeout_sec =
        xdebug_core::xdebug_session_idle_timeout_sec(timeout_error);
    if (idle_timeout_sec < 0) {
        return failure("INVALID_ENVIRONMENT", timeout_error,
                       "session_manager", false);
    }
    Json visible = Json::array();
    for (const SessionInfo& session : sessions) {
        visible.push_back(public_session(session));
    }
    Json data{{"sessions", visible}};
    return {{"ok", true},
            {"summary", {{"session_count", visible.size()},
                         {"expired_removed_count", 0}}},
            {"data", std::move(data)}};
}

Json doctor_session(const Json& request) {
    const std::string id = request["target"].value("session_id", std::string());
    SessionRegistry registry;
    SessionInfo session;
    const SessionRegistryResult found = registry.get(id, session);
    if (!found.ok()) return registry_lookup_failure(found);
    const HealthResult health = diagnose(session);
    if (!health.healthy) {
        return failure("SESSION_UNHEALTHY", health.message,
                       "session_manager", true,
                       {{"health_status", health.status},
                        {"session_id", session.session_id},
                        {"session_mode", session.dbdir_path.empty()
                             ? "waveform"
                             : (session.fsdb_file.empty() ? "design" : "combined")},
                        {"session_transport", session.transport}});
    }
    return {{"ok", true}, {"session", public_session(session)},
            {"summary", {{"healthy", true}}},
            {"data", {{"message", "Session is healthy"}}}};
}

Json remove_session(const Json& request, bool force) {
    const std::string id = request["target"].value("session_id", std::string());
    SessionRegistry registry;
    if (id == "all") {
        const Json args = request.value("args", Json::object());
        if (args.contains("ownership_token")) {
            return failure(
                "SESSION_OWNERSHIP_TOKEN_FORBIDDEN",
                "args.ownership_token is only valid for one exact session_id",
                "session_manager", false);
        }
        std::vector<SessionInfo> sessions;
        const SessionRegistryResult loaded = registry.load_all(sessions);
        if (!loaded.ok()) {
            return failure("SESSION_REGISTRY_FAILED", loaded.message);
        }
        Json removed_sessions = Json::array();
        Json failed_session_ids = Json::array();
        for (const SessionInfo& snapshot : sessions) {
            SessionLifecycleLease lease(snapshot.session_id);
            SessionInfo session;
            const SessionRegistryResult current =
                lease.locked()
                    ? registry.get(snapshot.session_id, session)
                    : SessionRegistryResult(
                          SessionRegistryStatus::IoError,
                          "failed to acquire the session lifecycle lease");
            if (current.status == SessionRegistryStatus::NotFound) {
                continue;
            }
            if (!current.ok() ||
                session.generation != snapshot.generation) {
                failed_session_ids.push_back(snapshot.session_id);
                continue;
            }
            if (cleanup_managed_session(registry, session, force)) {
                removed_sessions.push_back(public_session(session));
            } else {
                failed_session_ids.push_back(session.session_id);
            }
        }
        if (!failed_session_ids.empty()) {
            return failure(
                "SESSION_CLEANUP_PARTIAL_FAILURE",
                "one or more session engines could not be stopped",
                "session_manager", true,
                {{"requested_count", sessions.size()},
                 {"removed_count", removed_sessions.size()},
                 {"failed_session_ids", failed_session_ids}});
        }
        return {{"ok", true},
                {"summary", {{"requested_count", sessions.size()},
                             {"removed_count", removed_sessions.size()}}},
                {"data", {{"removed_sessions", removed_sessions}}}};
    }
    SessionLifecycleLease lease(id);
    if (!lease.locked()) {
        return failure(
            "SESSION_LIFECYCLE_LOCK_FAILED",
            "failed to acquire the session lifecycle lease");
    }
    SessionInfo session;
    const SessionRegistryResult found = registry.get(id, session);
    if (!found.ok()) return registry_lookup_failure(found);
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

    if (!cleanup_managed_session(registry, session, force)) {
        return failure("SESSION_CLEANUP_FAILED",
                       "session generation cleanup failed and evidence was retained",
                       "session_manager", true,
                       {{"cleanup_succeeded", false},
                        {"lifecycle_state", "cleanup_failed"},
                        {"compensation_status", "cleanup_failed"}});
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
    for (const SessionInfo& snapshot : sessions) {
        SessionLifecycleLease lease(snapshot.session_id);
        if (!lease.locked()) {
            kept.push_back(public_session(snapshot));
            continue;
        }
        SessionInfo session;
        const SessionRegistryResult current =
            registry.get(snapshot.session_id, session);
        if (current.status == SessionRegistryStatus::NotFound) continue;
        if (!current.ok() || session.generation != snapshot.generation) {
            kept.push_back(public_session(snapshot));
            continue;
        }
        const HealthResult health = diagnose(session);
        if (health.healthy) {
            kept.push_back(public_session(session));
            continue;
        }
        if (cleanup_managed_session(registry, session, true)) {
            removed.push_back({
                {"removed_session", public_session(session)},
                {"reason", "unhealthy"},
                {"health_evidence", {{"code", "SESSION_UNHEALTHY"},
                                     {"message", health.message},
                                     {"health_status", health.status}}}});
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
    if (!found.ok()) return registry_lookup_failure(found);
    if (session.lifecycle_state != "active") {
        return failure("SESSION_UNHEALTHY", "session generation is not active");
    }
    Json response;
    std::string error;
    if (!uds_request(session.socket_path, request, response, 30000, error)) {
        return failure("TRANSPORT_FAILED", error, "transport");
    }
    return response;
}

}  // namespace xdebug_fst
