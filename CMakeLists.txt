cmake_minimum_required(VERSION 3.20)
project(claudius VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Platform detection ---
if(APPLE)
    add_compile_definitions(CLAUDIUS_MACOS)
elseif(UNIX)
    add_compile_definitions(CLAUDIUS_LINUX)
endif()

# --- Sanitizer options (debug builds) ---
option(CLAUDIUS_ASAN "Enable AddressSanitizer" OFF)
if(CLAUDIUS_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# --- Find system deps ---
find_package(Threads REQUIRED)

# OpenSSL / LibreSSL for TLS
find_package(OpenSSL REQUIRED)

# --- Source files ---
set(CLAUDIUS_SOURCES
    src/main.cpp
    src/api_client.cpp
    src/agent.cpp
    src/constitution.cpp
    src/orchestrator.cpp
    src/server.cpp
    src/auth.cpp
    src/json.cpp
    src/commands.cpp
    src/readline_wrapper.cpp
    src/cost_tracker.cpp
    src/markdown.cpp
)

add_executable(claudius ${CLAUDIUS_SOURCES})

target_include_directories(claudius PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(claudius PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# --- Readline / libedit for interactive line editing ---
# Try libedit first (BSD license, ships on macOS)
find_library(EDITLINE_LIB edit)
find_path(EDITLINE_INCLUDE editline/readline.h)

if(NOT EDITLINE_LIB OR NOT EDITLINE_INCLUDE)
    find_library(EDITLINE_LIB readline)
    find_path(EDITLINE_INCLUDE readline/readline.h)
    if(EDITLINE_LIB AND EDITLINE_INCLUDE)
        set(READLINE_GNU ON)
    endif()
endif()

if(EDITLINE_LIB AND EDITLINE_INCLUDE)
    message(STATUS "Found readline/libedit: ${EDITLINE_LIB}")
    target_compile_definitions(claudius PRIVATE CLAUDIUS_HAS_READLINE)
    if(NOT READLINE_GNU)
        target_compile_definitions(claudius PRIVATE CLAUDIUS_USE_EDITLINE)
    endif()
    target_include_directories(claudius PRIVATE ${EDITLINE_INCLUDE})
    target_link_libraries(claudius PRIVATE ${EDITLINE_LIB})
else()
    message(WARNING "readline/libedit not found; falling back to raw getline (no line editing)")
endif()

# --- Install ---
install(TARGETS claudius RUNTIME DESTINATION bin)
install(DIRECTORY agents/ DESTINATION share/claudius/agents
        FILES_MATCHING PATTERN "*.json")
