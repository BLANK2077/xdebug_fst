cmake_minimum_required(VERSION 3.16)
project(xdebug-fst VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_SOURCE_DIR}/cmake/DependenciesLock.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/VerifyDependencies.cmake)

# ── wellen_capi (core FFI, Phase 1 artifact in wellen workspace) ──
set(WELLEN_CAPI_DIR "${CMAKE_SOURCE_DIR}/../wellen/wellen_capi" CACHE PATH "wellen_capi root")
set(WELLEN_CAPI_INCLUDE "${WELLEN_CAPI_DIR}/include")
set(WELLEN_CAPI_LIB_DIR "${CMAKE_SOURCE_DIR}/../wellen/target/release" CACHE PATH "wellen_capi build dir")
get_filename_component(WELLEN_REPOSITORY_DIR "${WELLEN_CAPI_DIR}/.." ABSOLUTE)

# ── wellenx_capi (minimal extension FFI, built inside this repo) ──
set(WELLENX_CAPI_INCLUDE "${CMAKE_SOURCE_DIR}/wellenx_capi/include")
set(WELLENX_CAPI_LIB_DIR "${CMAKE_SOURCE_DIR}/wellenx_capi/target/release" CACHE PATH "wellenx_capi build dir")

set(VERILATOR_REPOSITORY_DIR "${CMAKE_SOURCE_DIR}/../verilator" CACHE PATH
    "Verilator source tree that provides the compatible DesignDB ABI")
option(XDEBUG_VERIFY_DEPENDENCY_REVISIONS
    "Require the exact Wellen and Verilator revisions from the compatibility lock" ON)

if(XDEBUG_VERIFY_DEPENDENCY_REVISIONS)
    xdebug_require_git_revision("Wellen" "${WELLEN_REPOSITORY_DIR}" "${XDEBUG_WELLEN_REVISION}")
    xdebug_require_git_revision("Verilator" "${VERILATOR_REPOSITORY_DIR}" "${XDEBUG_VERILATOR_REVISION}")
endif()

xdebug_require_file_hash("Wellen C ABI header"
    "${WELLEN_CAPI_INCLUDE}/wellen_capi.h" "${XDEBUG_WELLEN_CAPI_HEADER_SHA256}")
xdebug_require_file_hash("Verilator DesignDB ABI header"
    "${VERILATOR_REPOSITORY_DIR}/include/xdd_api.h" "${XDEBUG_XDD_HEADER_SHA256}")
xdebug_require_file_hash("wellenx C ABI header"
    "${WELLENX_CAPI_INCLUDE}/wellenx_capi.h" "${XDEBUG_WELLENX_CAPI_HEADER_SHA256}")

find_library(WELLEN_CAPI_LIBRARY
    NAMES wellen_capi
    PATHS "${WELLEN_CAPI_LIB_DIR}"
    NO_DEFAULT_PATH)
if(NOT WELLEN_CAPI_LIBRARY)
    message(FATAL_ERROR
        "Missing release wellen_capi library in ${WELLEN_CAPI_LIB_DIR}. "
        "Build it with: cargo build --release -p wellen-capi")
endif()

find_library(WELLENX_CAPI_LIBRARY
    NAMES wellenx_capi
    PATHS "${WELLENX_CAPI_LIB_DIR}"
    NO_DEFAULT_PATH)
if(NOT WELLENX_CAPI_LIBRARY)
    message(FATAL_ERROR
        "Missing release wellenx_capi library in ${WELLENX_CAPI_LIB_DIR}. "
        "Build it with: cargo build --release --manifest-path wellenx_capi/Cargo.toml")
endif()

# ── Sources (glob: new action files are picked up automatically) ──
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

set(JSON_SCHEMA_VALIDATOR_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/smtp-address-validator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/json-schema-draft7.json.cpp
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/json-uri.cpp
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/json-validator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/json-patch.cpp
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src/string-format-check.cpp
)

# ── Executable ──
add_executable(xdebug-fst ${SOURCES} ${JSON_SCHEMA_VALIDATOR_SOURCES})

target_include_directories(xdebug-fst PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/third_party
    ${WELLEN_CAPI_INCLUDE}
    ${WELLENX_CAPI_INCLUDE}
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src
)

target_compile_definitions(xdebug-fst PRIVATE
    XDEBUG_FST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(xdebug-fst PRIVATE
    "${WELLEN_CAPI_LIBRARY}"
    "${WELLENX_CAPI_LIBRARY}"
    dl
    pthread
)

set_target_properties(xdebug-fst PROPERTIES
    BUILD_RPATH "${WELLEN_CAPI_LIB_DIR};${WELLENX_CAPI_LIB_DIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE)

# ── Install ──
install(TARGETS xdebug-fst DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(test-runtime-schema-validator
        tests/cpp/test_runtime_schema_validator.cpp
        src/core/common/env_config.cpp
        src/core/schema/internal_request_contract.cpp
        src/core/schema/runtime_schema_validator.cpp
        ${JSON_SCHEMA_VALIDATOR_SOURCES}
    )
    target_include_directories(test-runtime-schema-validator PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src
    )
    target_compile_definitions(test-runtime-schema-validator PRIVATE
        XDEBUG_FST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(test-runtime-schema-validator PRIVATE pthread stdc++fs)
    add_test(NAME runtime-schema-validator COMMAND test-runtime-schema-validator)
endif()
