cmake_minimum_required(VERSION 3.16)
project(xdebug-fst VERSION 0.1.0 LANGUAGES CXX C)

set(XDEBUG_RELEASE_VERSION "${PROJECT_VERSION}" CACHE STRING "Product release version")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE XDEBUG_GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT XDEBUG_GIT_REVISION)
    if(EXISTS "${CMAKE_SOURCE_DIR}/SOURCE_REVISION")
        file(READ "${CMAKE_SOURCE_DIR}/SOURCE_REVISION" XDEBUG_GIT_REVISION)
        string(STRIP "${XDEBUG_GIT_REVISION}" XDEBUG_GIT_REVISION)
    else()
        message(FATAL_ERROR "Source revision unavailable: use Git checkout or release source archive")
    endif()
endif()
configure_file(cmake/build_version.h.in generated/build_version.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XDEBUG_DEPENDENCY_ROOT "${CMAKE_BINARY_DIR}/_deps" CACHE PATH
    "Prepared dependency shadow source root")
set(XDEBUG_TOOLCHAIN_ROOT "" CACHE PATH "Required xdebug private GCC toolchain")
set(WELLEN_REPOSITORY_DIR "${XDEBUG_DEPENDENCY_ROOT}/wellen-src")
set(VERILATOR_REPOSITORY_DIR "${XDEBUG_DEPENDENCY_ROOT}/verilator-src")
set(WELLEN_CAPI_INCLUDE "${WELLEN_REPOSITORY_DIR}/wellen_capi/include")
set(WELLENX_CAPI_INCLUDE "${WELLEN_REPOSITORY_DIR}/wellenx_capi/include")
set(WELLEN_CAPI_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
set(WELLENX_CAPI_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
set(WELLEN_CAPI_LIBRARY "${WELLEN_CAPI_LIB_DIR}/libwellen_capi.so")
set(WELLENX_CAPI_LIBRARY "${WELLENX_CAPI_LIB_DIR}/libwellenx_capi.so")

foreach(REQUIRED_PATH IN ITEMS
        "${WELLEN_CAPI_INCLUDE}/wellen_capi.h"
        "${WELLENX_CAPI_INCLUDE}/wellenx_capi.h"
        "${VERILATOR_REPOSITORY_DIR}/include/xdd_api.h"
        "${WELLEN_CAPI_LIBRARY}"
        "${WELLENX_CAPI_LIBRARY}")
    if(NOT EXISTS "${REQUIRED_PATH}")
        message(FATAL_ERROR
            "Missing prepared dependency artifact: ${REQUIRED_PATH}. "
            "Configure through tools/build.sh; dependency fallback is disabled.")
    endif()
endforeach()

if(NOT IS_ABSOLUTE "${XDEBUG_TOOLCHAIN_ROOT}" OR
   NOT EXISTS "${XDEBUG_TOOLCHAIN_ROOT}/bin/gcc" OR
   NOT EXISTS "${XDEBUG_TOOLCHAIN_ROOT}/bin/g++")
    message(FATAL_ERROR "XDEBUG_TOOLCHAIN_ROOT must identify the private GCC toolchain")
endif()
get_filename_component(XDEBUG_EXPECTED_CC "${XDEBUG_TOOLCHAIN_ROOT}/bin/gcc" REALPATH)
get_filename_component(XDEBUG_EXPECTED_CXX "${XDEBUG_TOOLCHAIN_ROOT}/bin/g++" REALPATH)
get_filename_component(XDEBUG_ACTUAL_CC "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(XDEBUG_ACTUAL_CXX "${CMAKE_CXX_COMPILER}" REALPATH)
if(NOT XDEBUG_ACTUAL_CC STREQUAL XDEBUG_EXPECTED_CC OR
   NOT XDEBUG_ACTUAL_CXX STREQUAL XDEBUG_EXPECTED_CXX)
    message(FATAL_ERROR
        "System compiler fallback is forbidden. Expected ${XDEBUG_EXPECTED_CC} and "
        "${XDEBUG_EXPECTED_CXX}; got ${XDEBUG_ACTUAL_CC} and ${XDEBUG_ACTUAL_CXX}")
endif()
execute_process(COMMAND "${XDEBUG_EXPECTED_CC}" -dumpfullversion
    OUTPUT_VARIABLE XDEBUG_GCC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XDEBUG_GCC_VERSION STREQUAL "13.3.1")
    message(FATAL_ERROR "Private GCC version must be 13.3.1, got ${XDEBUG_GCC_VERSION}")
endif()

option(XDEBUG_ENABLE_MCP_INTEGRATION_TESTS
    "Run integration tests against the adjacent xverif MCP loop wrapper" OFF)
option(XDEBUG_ENABLE_ASAN
    "Build xdebug-fst and its C/C++ tests with AddressSanitizer" OFF)
option(XDEBUG_ENABLE_UBSAN
    "Build xdebug-fst and its C/C++ tests with UndefinedBehaviorSanitizer" OFF)
set(XVERIF_MCP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../../xverif/xverif_mcp/src"
    CACHE PATH "xverif_mcp source directory used only by optional integration tests")

if(XDEBUG_ENABLE_ASAN AND XDEBUG_ENABLE_UBSAN)
    message(FATAL_ERROR
        "XDEBUG_ENABLE_ASAN and XDEBUG_ENABLE_UBSAN are independent gates; "
        "enable only one per build directory")
endif()

if(XDEBUG_ENABLE_ASAN OR XDEBUG_ENABLE_UBSAN)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
       NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "xdebug-fst sanitizer gates require GNU or Clang C/C++ compilers")
    endif()

    add_compile_options(-fno-omit-frame-pointer)
    if(XDEBUG_ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
        message(STATUS "xdebug-fst AddressSanitizer gate enabled")
    else()
        add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
        add_link_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
        message(STATUS "xdebug-fst UndefinedBehaviorSanitizer gate enabled")
    endif()
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
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/third_party
    ${WELLEN_CAPI_INCLUDE}
    ${WELLENX_CAPI_INCLUDE}
    ${CMAKE_SOURCE_DIR}/third_party/json-schema-validator/src
)

# Build and installed executables use the same relative data layout.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/xdebug-fst")
file(COPY "${CMAKE_SOURCE_DIR}/compat/xdebug-v1/"
    DESTINATION "${CMAKE_BINARY_DIR}/share/xdebug-fst")

target_link_libraries(xdebug-fst PRIVATE
    "${WELLEN_CAPI_LIBRARY}"
    "${WELLENX_CAPI_LIBRARY}"
    dl
    pthread
    stdc++fs
)

set_target_properties(xdebug-fst PROPERTIES
    BUILD_RPATH "${WELLEN_CAPI_LIB_DIR};${WELLENX_CAPI_LIB_DIR};${XDEBUG_TOOLCHAIN_ROOT}/lib64"
    INSTALL_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE)

# ── Install ──
install(TARGETS xdebug-fst DESTINATION bin)
install(FILES "${WELLEN_CAPI_LIBRARY}" "${WELLENX_CAPI_LIBRARY}" DESTINATION lib)
install(DIRECTORY "${CMAKE_BINARY_DIR}/share/xdebug-fst/" DESTINATION share/xdebug-fst)
install(DIRECTORY "${CMAKE_BINARY_DIR}/tools/verilator/" DESTINATION tools/verilator
    USE_SOURCE_PERMISSIONS PATTERN ".xdebug-build-stamp" EXCLUDE)
install(FILES LICENSE THIRD_PARTY.md DESTINATION share/licenses/xdebug-fst)
install(DIRECTORY licenses/ DESTINATION share/licenses/xdebug-fst)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
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

    add_executable(test-session-registry
        tests/cpp/test_session_registry.cpp
        src/core/common/env_config.cpp
        src/core/common/path_utils.cpp
        src/core/common/tool_config.cpp
        src/core/session/session_endpoint_contract.cpp
        src/core/session/session_registry_contract.cpp
        src/core/session/session_types.cpp
        src/session/session_paths.cpp
        src/session/session_registry.cpp
    )
    target_include_directories(test-session-registry PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_link_libraries(test-session-registry PRIVATE stdc++fs)
    add_test(NAME session-registry COMMAND test-session-registry)

    add_executable(test-uds-transport
        tests/cpp/test_uds_transport.cpp
        src/session/uds_transport.cpp
    )
    target_include_directories(test-uds-transport PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_link_libraries(test-uds-transport PRIVATE pthread)
    add_test(NAME uds-transport COMMAND test-uds-transport)

    add_library(test-xdd-v1-bundle SHARED
        tests/cpp/xdd_v1_bundle.cpp
    )

    set(P3B_ACTIVE_DRIVER_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/active_driver/obj_dir")
    file(MAKE_DIRECTORY "${P3B_ACTIVE_DRIVER_XDD_BUNDLE_DIR}")
    add_library(test-p3b-active-driver-xdd-bundle SHARED
        testdata/fixtures/active_driver/Vactive_driver_tb__DesignDb.cpp
    )
    target_include_directories(test-p3b-active-driver-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    set_target_properties(test-p3b-active-driver-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vactive_driver_tb__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${P3B_ACTIVE_DRIVER_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${P3B_ACTIVE_DRIVER_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVactive_driver_tb__DesignDb.so\"\n}\n"
    )

    set(P3B_INTERFACE_ROOT_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/interface_port_root/obj_dir")
    file(MAKE_DIRECTORY "${P3B_INTERFACE_ROOT_XDD_BUNDLE_DIR}")
    add_library(test-p3b-interface-root-xdd-bundle SHARED
        testdata/fixtures/interface_port_root/Vif_root_tb__DesignDb.cpp
    )
    target_include_directories(test-p3b-interface-root-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    set_target_properties(test-p3b-interface-root-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vif_root_tb__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${P3B_INTERFACE_ROOT_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${P3B_INTERFACE_ROOT_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVif_root_tb__DesignDb.so\"\n}\n"
    )

    set(P3B_ACTIVE_ZERO_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/active_zero_evidence/obj_dir")
    file(MAKE_DIRECTORY "${P3B_ACTIVE_ZERO_XDD_BUNDLE_DIR}")
    add_library(test-p3b-active-zero-xdd-bundle SHARED
        testdata/fixtures/active_zero_evidence/Vactive_zero_evidence_tb__DesignDb.cpp
    )
    target_include_directories(test-p3b-active-zero-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    set_target_properties(test-p3b-active-zero-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vactive_zero_evidence_tb__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${P3B_ACTIVE_ZERO_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${P3B_ACTIVE_ZERO_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVactive_zero_evidence_tb__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/gcd_xorigin/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    set_target_properties(test-xorigin-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vgcd_xorigin__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVgcd_xorigin__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_PREDICATE_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/gcd_xpredicate/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_PREDICATE_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-predicate-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-predicate-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-predicate-xdd-bundle PRIVATE
        XDEBUG_TEST_X_PREDICATE=1
    )
    set_target_properties(test-xorigin-predicate-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vgcd_xpredicate__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_PREDICATE_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_PREDICATE_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVgcd_xpredicate__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_UNRESOLVED_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/gcd_unresolved/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_UNRESOLVED_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-unresolved-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-unresolved-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-unresolved-xdd-bundle PRIVATE
        XDEBUG_TEST_MISSING_PREDICATE=1
    )
    set_target_properties(test-xorigin-unresolved-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vgcd_unresolved__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_UNRESOLVED_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_UNRESOLVED_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVgcd_unresolved__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_TIME_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_time/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_TIME_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-time-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-time-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-time-xdd-bundle PRIVATE
        XDEBUG_TEST_X_TIME_LIMIT=1
    )
    set_target_properties(test-xorigin-time-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_time__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_TIME_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_TIME_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_time__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_ALIAS_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_alias/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_ALIAS_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-alias-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-alias-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-alias-xdd-bundle PRIVATE
        XDEBUG_TEST_ALIAS_COALESCE=1
    )
    set_target_properties(test-xorigin-alias-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_alias__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_ALIAS_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_ALIAS_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_alias__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_MODPORT_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_modport/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_MODPORT_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-modport-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-modport-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-modport-xdd-bundle PRIVATE
        XDEBUG_TEST_MODPORT_ALIAS=1
    )
    set_target_properties(test-xorigin-modport-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_modport__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_MODPORT_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_MODPORT_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_modport__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_REF_PORT_LOOP_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_ref_port_loop/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_REF_PORT_LOOP_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-ref-port-loop-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-ref-port-loop-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-ref-port-loop-xdd-bundle PRIVATE
        XDEBUG_TEST_REF_PORT_LOOP=1
    )
    set_target_properties(test-xorigin-ref-port-loop-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_ref_port_loop__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_REF_PORT_LOOP_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_REF_PORT_LOOP_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_ref_port_loop__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_LOOP_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_loop/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_LOOP_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-loop-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-loop-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-loop-xdd-bundle PRIVATE
        XDEBUG_TEST_X_LOOP=1
    )
    set_target_properties(test-xorigin-loop-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_loop__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_LOOP_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_LOOP_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_loop__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_LOOP_BRANCH_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_loop_branch/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_LOOP_BRANCH_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-loop-branch-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-loop-branch-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-loop-branch-xdd-bundle PRIVATE
        XDEBUG_TEST_X_LOOP_BRANCH=1
    )
    set_target_properties(test-xorigin-loop-branch-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_loop_branch__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_LOOP_BRANCH_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_LOOP_BRANCH_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_loop_branch__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_PATTERNVAR_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_patternvar/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_PATTERNVAR_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-patternvar-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-patternvar-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-patternvar-xdd-bundle PRIVATE
        XDEBUG_TEST_PATTERNVAR_BINDING=1
    )
    set_target_properties(test-xorigin-patternvar-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_patternvar__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_PATTERNVAR_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_PATTERNVAR_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_patternvar__DesignDb.so\"\n}\n"
    )

    set(XORIGIN_REF_DRIVER_BRANCH_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/xorigin_ref_driver_branch/obj_dir")
    file(MAKE_DIRECTORY "${XORIGIN_REF_DRIVER_BRANCH_XDD_BUNDLE_DIR}")
    add_library(test-xorigin-ref-driver-branch-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-xorigin-ref-driver-branch-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-xorigin-ref-driver-branch-xdd-bundle PRIVATE
        XDEBUG_TEST_REF_DRIVER_BRANCH=1
    )
    set_target_properties(test-xorigin-ref-driver-branch-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vxorigin_ref_driver_branch__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${XORIGIN_REF_DRIVER_BRANCH_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${XORIGIN_REF_DRIVER_BRANCH_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVxorigin_ref_driver_branch__DesignDb.so\"\n}\n"
    )

    set(PRIMITIVE_OUTPUT_XDD_BUNDLE_DIR
        "${CMAKE_BINARY_DIR}/testdata/fixtures/primitive_output/obj_dir")
    file(MAKE_DIRECTORY "${PRIMITIVE_OUTPUT_XDD_BUNDLE_DIR}")
    add_library(test-primitive-output-xdd-bundle SHARED
        tests/cpp/xorigin_xdd_bundle.cpp
    )
    target_include_directories(test-primitive-output-xdd-bundle PRIVATE
        ${VERILATOR_REPOSITORY_DIR}/include
    )
    target_compile_definitions(test-primitive-output-xdd-bundle PRIVATE
        XDEBUG_TEST_PRIMITIVE_OUTPUT=1
    )
    set_target_properties(test-primitive-output-xdd-bundle PROPERTIES
        OUTPUT_NAME "Vprimitive_output__DesignDb"
        LIBRARY_OUTPUT_DIRECTORY "${PRIMITIVE_OUTPUT_XDD_BUNDLE_DIR}"
    )
    file(GENERATE
        OUTPUT "${PRIMITIVE_OUTPUT_XDD_BUNDLE_DIR}/xdebug-design-db.json"
        CONTENT "{\n  \"schema_version\": \"xdebug.design-db-bundle.v1\",\n  \"library\": \"libVprimitive_output__DesignDb.so\"\n}\n"
    )

    add_executable(test-xdd-design-backend
        tests/cpp/test_xdd_design_backend.cpp
        src/backend/xdd_design_backend.cpp
    )
    target_include_directories(test-xdd-design-backend PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test-xdd-design-backend PRIVATE dl)
    add_test(
        NAME xdd-design-backend
        COMMAND test-xdd-design-backend
            ${CMAKE_SOURCE_DIR}/testdata/fixtures/counter/obj_dir/libVcounter_top__DesignDb.so
            $<TARGET_FILE:test-xdd-v1-bundle>
    )

    add_executable(test-wellen-fst-backend
        tests/cpp/test_wellen_fst_backend.cpp
        src/backend/wellen_fst_backend.cpp
        src/waveform/clock_sampling.cpp
        src/waveform/time_contract.cpp
    )
    target_include_directories(test-wellen-fst-backend PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WELLEN_CAPI_INCLUDE}
        ${WELLENX_CAPI_INCLUDE}
    )
    target_link_libraries(test-wellen-fst-backend PRIVATE
        "${WELLEN_CAPI_LIBRARY}"
        "${WELLENX_CAPI_LIBRARY}"
    )
    set_target_properties(test-wellen-fst-backend PROPERTIES
        BUILD_RPATH "${WELLEN_CAPI_LIB_DIR};${WELLENX_CAPI_LIB_DIR};${XDEBUG_TOOLCHAIN_ROOT}/lib64")
    add_test(
        NAME wellen-fst-backend
        COMMAND test-wellen-fst-backend
            ${WELLEN_REPOSITORY_DIR}/wellen/inputs/scope_with_comment.vcd.fst
            ${WELLEN_REPOSITORY_DIR}/wellen/inputs/xilinx_isim/test2x2_regex22_string1.vcd.fst
            ${WELLEN_REPOSITORY_DIR}/wellen/inputs/nvc/shortstring.fst
            ${WELLEN_REPOSITORY_DIR}/wellen/inputs/verilator/many_sv_datatypes.fst
            ${WELLEN_REPOSITORY_DIR}/wellen/inputs/icarus/pull_67_event_example.fst
            ${CMAKE_SOURCE_DIR}/testdata/fixtures/phase5/waves.fst
    )

    add_test(
        NAME session-uds-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_session_uds_lifecycle.py
            $<TARGET_FILE:xdebug-fst>
            ${CMAKE_SOURCE_DIR}/testdata/fixtures/waves.fst
            ${CMAKE_SOURCE_DIR}/testdata/fixtures/counter/obj_dir/libVcounter_top__DesignDb.so
    )

    add_test(
        NAME session-stability
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_session_stability.py
            $<TARGET_FILE:xdebug-fst>
            ${CMAKE_SOURCE_DIR}/testdata/fixtures/waves.fst
    )
    set_tests_properties(session-stability PROPERTIES TIMEOUT 120)

    if(XDEBUG_ENABLE_MCP_INTEGRATION_TESTS)
        if(NOT EXISTS "${XVERIF_MCP_SOURCE_DIR}/xverif_mcp/adapters/xdebug.py")
            message(FATAL_ERROR
                "XDEBUG_ENABLE_MCP_INTEGRATION_TESTS requires a valid "
                "XVERIF_MCP_SOURCE_DIR, got: ${XVERIF_MCP_SOURCE_DIR}")
        endif()
        foreach(MCP_MODE direct fake-lsf)
            add_test(
                NAME mcp-${MCP_MODE}-lifecycle
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/test_mcp_lifecycle_integration.py
                    $<TARGET_FILE:xdebug-fst>
                    ${CMAKE_SOURCE_DIR}/testdata/fixtures/waves.fst
                    ${XVERIF_MCP_SOURCE_DIR}
                    ${MCP_MODE}
            )
            set_tests_properties(mcp-${MCP_MODE}-lifecycle PROPERTIES
                TIMEOUT 30)
        endforeach()
    endif()
endif()
