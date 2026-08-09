#include "session/session_registry.h"
#include "session/session_endpoint_contract.h"
#include "session/session_registry_contract.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using xdebug_engine::SessionInfo;
using xdebug_engine::SessionRegistry;
using xdebug_engine::SessionRegistryStatus;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "session registry test failed: " << message << '\n';
        std::exit(1);
    }
}

SessionInfo opening_session(const std::string& generation) {
    SessionInfo session;
    session.session_id = "case_a";
    session.generation = generation;
    session.lifecycle_state = "opening";
    session.transport = "uds";
    session.socket_path = "/tmp/xdebug-case-a.sock";
    session.server_host = "localhost";
    session.fsdb_file = "/tmp/waves.fst";
    session.created_at = 100;
    session.last_active = 100;
    return session;
}

}  // namespace

int main() {
    char directory[] = "/tmp/xdebug-fst-registry-XXXXXX";
    char* root = mkdtemp(directory);
    require(root != nullptr, "mkdtemp failed");
    setenv("HOME", root, 1);
    setenv("XVERIF_TEST_TMPDIR", root, 1);

    const std::string generation(64, 'a');
    const std::string other_generation(64, 'b');
    SessionRegistry registry;
    SessionInfo opening = opening_session(generation);
    require(registry.reserve_opening(opening).ok(), "opening reservation failed");
    require(registry.reserve_opening(opening).status == SessionRegistryStatus::Conflict,
            "duplicate session name was not rejected");

    opening.port = 0;
    opening.last_active = 101;
    require(registry.update_opening(opening, generation).ok(),
            "opening evidence update failed");

    SessionInfo active = opening;
    active.lifecycle_state = "active";
    active.server_pid = 12345;
    require(registry.finalize_opening(active, generation).ok(),
            "opening to active compare-and-swap failed");
    require(registry.touch_if_generation("case_a", generation, 120).ok(),
            "active touch failed");
    require(registry.touch_if_generation("case_a", generation, 110).ok(),
            "older touch should be idempotent");

    SessionInfo loaded;
    require(registry.get("case_a", loaded).ok(), "active session lookup failed");
    require(loaded.generation == generation && loaded.lifecycle_state == "active" &&
                loaded.last_active == 120,
            "persisted active generation differs");

    SessionRegistry second_process_view;
    std::vector<SessionInfo> sessions;
    require(second_process_view.load_all(sessions).ok() && sessions.size() == 1,
            "second registry instance did not observe persisted generation");
    require(registry.remove_if_generation("case_a", other_generation).status ==
                SessionRegistryStatus::GenerationMismatch,
            "generation mismatch did not protect removal");
    require(registry.remove_if_generation("case_a", generation).ok(),
            "conditional removal failed");
    require(registry.get("case_a", loaded).status == SessionRegistryStatus::NotFound,
            "removed session remains visible");

    nlohmann::json endpoint;
    std::string error;
    require(xdebug_core::session_endpoint_document_to_json(active, endpoint, error),
            "endpoint serialization failed: " + error);
    SessionInfo endpoint_roundtrip;
    require(xdebug_core::session_endpoint_document_from_json(
                endpoint, "case_a", endpoint_roundtrip, error),
            "endpoint parse failed: " + error);
    require(endpoint_roundtrip.transport == "uds" &&
                endpoint_roundtrip.socket_path == active.socket_path,
            "endpoint roundtrip differs");

    endpoint["unexpected"] = true;
    require(!xdebug_core::session_endpoint_document_from_json(
                endpoint, "case_a", endpoint_roundtrip, error),
            "endpoint accepted an unknown field");

    std::filesystem::remove_all(root);
    return 0;
}
