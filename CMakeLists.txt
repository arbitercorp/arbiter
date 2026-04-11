cmake_minimum_required(VERSION 3.20)
project(claudius VERSION 0.1.3 LANGUAGES CXX)

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

# --- Install ---
install(TARGETS claudius RUNTIME DESTINATION bin)
install(DIRECTORY agents/ DESTINATION share/claudius/agents
        FILES_MATCHING PATTERN "*.json")
