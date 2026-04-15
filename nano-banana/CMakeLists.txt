cmake_minimum_required(VERSION 3.10)
project(nexus_inventory C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# Find required packages
find_package(SQLite3 REQUIRED)
find_package(MicroHTTPD REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.c
    src/db.c
    src/auth.c
    src/api.c
)

# Create executable
add_executable(nexus_server ${SOURCES})

# Link libraries
target_link_libraries(nexus_server
    SQLite::SQLite3
    MicroHTTPD::MicroHTTPD
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# Install target
install(TARGETS nexus_server DESTINATION bin)

# Create frontend directory if it doesn't exist
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/frontend")
