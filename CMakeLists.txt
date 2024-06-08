cmake_minimum_required(VERSION 3.13)
project(http-server-starter-cpp)
set(CMAKE_CXX_STANDARD 20) # Enable the C++20 standard

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)

# Add the executable
add_executable(server ${SOURCE_FILES})

# Find ZLIB library
find_package(ZLIB REQUIRED)

# Link libraries
target_link_libraries(server pthread ZLIB::ZLIB)
