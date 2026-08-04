cmake_minimum_required(VERSION 3.16)
project(xdebug-fst VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── wellen_capi ──
# Path to wellen_capi header and library
set(WELLEN_CAPI_DIR "${CMAKE_SOURCE_DIR}/../wellen/wellen_capi" CACHE PATH "wellen_capi root")
set(WELLEN_CAPI_INCLUDE "${WELLEN_CAPI_DIR}/include")
set(WELLEN_CAPI_LIB_DIR "${CMAKE_SOURCE_DIR}/../wellen/target/release" CACHE PATH "wellen_capi build dir")

# ── Sources ──
set(SOURCES
    src/main.cpp
    src/server.cpp
    src/engine/engine_globals.cpp
    src/backend/wellen_fst_backend.cpp
    src/backend/xdd_design_backend.cpp
)

set(HEADERS
    src/backend/waveform_backend.h
    src/backend/design_backend.h
    src/backend/wellen_fst_backend.h
    src/backend/xdd_design_backend.h
    src/engine/engine_globals.h
    src/engine/engine_action_handler.h
)

# ── Executable ──
add_executable(xdebug-fst ${SOURCES} ${HEADERS})

target_include_directories(xdebug-fst PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${WELLEN_CAPI_INCLUDE}
)

target_link_directories(xdebug-fst PRIVATE ${WELLEN_CAPI_LIB_DIR})
target_link_libraries(xdebug-fst PRIVATE
    wellen_capi
    dl
    pthread
)

# ── Install ──
install(TARGETS xdebug-fst DESTINATION bin)
