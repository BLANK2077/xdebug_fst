cmake_minimum_required(VERSION 3.16)
project(xdebug-fst VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── wellen_capi (core FFI, Phase 1 artifact in wellen workspace) ──
set(WELLEN_CAPI_DIR "${CMAKE_SOURCE_DIR}/../wellen/wellen_capi" CACHE PATH "wellen_capi root")
set(WELLEN_CAPI_INCLUDE "${WELLEN_CAPI_DIR}/include")
set(WELLEN_CAPI_LIB_DIR "${CMAKE_SOURCE_DIR}/../wellen/target/release" CACHE PATH "wellen_capi build dir")

# ── wellenx_capi (minimal extension FFI, built inside this repo) ──
set(WELLENX_CAPI_INCLUDE "${CMAKE_SOURCE_DIR}/wellenx_capi/include")
set(WELLENX_CAPI_LIB_DIR "${CMAKE_SOURCE_DIR}/wellenx_capi/target/release" CACHE PATH "wellenx_capi build dir")

# ── Sources ──
set(SOURCES
    src/main.cpp
    src/server.cpp
    src/engine/engine_globals.cpp
    src/engine/action_registry.cpp
    src/engine/actions/register_all.cpp
    src/backend/wellen_fst_backend.cpp
    src/backend/xdd_design_backend.cpp
    src/core/value/logic_value.cpp
    src/waveform/clock_sampling.cpp
    src/waveform/list/list_manager.cpp
    src/waveform/cursor/cursor_manager.cpp
    src/waveform/expr/expr_eval.cpp
    src/engine/actions/waveform/scope_list.cpp
    src/engine/actions/waveform/value_at.cpp
    src/engine/actions/design/signal_resolve.cpp
)

# ── Executable ──
add_executable(xdebug-fst ${SOURCES})

target_include_directories(xdebug-fst PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${WELLEN_CAPI_INCLUDE}
    ${WELLENX_CAPI_INCLUDE}
)

target_link_directories(xdebug-fst PRIVATE
    ${WELLEN_CAPI_LIB_DIR}
    ${WELLENX_CAPI_LIB_DIR}
)
target_link_libraries(xdebug-fst PRIVATE
    wellen_capi
    wellenx_capi
    dl
    pthread
)

# ── Install ──
install(TARGETS xdebug-fst DESTINATION bin)
