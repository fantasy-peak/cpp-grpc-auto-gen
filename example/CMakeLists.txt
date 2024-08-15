cmake_minimum_required(VERSION 3.22.1)

project(test)

# Version
set(VERSION 2.2.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build
set(CMAKE_CXX_STANDARD 23)
include(FetchContent)
include(${CMAKE_SOURCE_DIR}/cmake/grpc.cmake)

message(
  STATUS
    "Downloading and extracting boost library sources. This will take some time..."
)
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(BOOST_INCLUDE_LIBRARIES regex;asio;circular_buffer;signals2)
FetchContent_Declare(
  Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.86.0.beta1/boost-1.86.0.beta1-cmake.tar.gz
      USES_TERMINAL_DOWNLOAD
      TRUE
  GIT_PROGRESS TRUE
  DOWNLOAD_NO_EXTRACT FALSE)
FetchContent_MakeAvailable(Boost)

FetchContent_Declare(
        asio-grpc
        GIT_REPOSITORY https://github.com/Tradias/asio-grpc.git
        GIT_TAG        master
)
FetchContent_MakeAvailable(asio-grpc)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
)
FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
    FetchContent_Populate(spdlog)
    add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif ()

add_compile_options(
  # -Werror
  -pedantic-errors
  -O2
  -Wall
  -Wno-missing-field-initializers
  -Wformat
  -Wfloat-equal
  -D_REENTRANT
  -pipe
  -fno-omit-frame-pointer -fsanitize=address)

# find_package(asio CONFIG REQUIRED)
# find_package(fmt CONFIG REQUIRED)
# 会强制找 grpc 会产生错误 find_package(asio-grpc CONFIG REQUIRED)

add_definitions(-DAGRPC_BOOST_ASIO=1)

file(GLOB_RECURSE SRCS src/server.cpp)
add_executable(grpc-server ${SRCS})
target_include_directories(grpc-server BEFORE PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_add_protobuf(grpc-server example.proto)
target_add_protobuf(grpc-server health.proto)
target_link_libraries(grpc-server PUBLIC asan Boost::asio Boost::circular_buffer Boost::signals2 ssl crypto asio-grpc spdlog::spdlog
                                    pthread dl)


add_executable(client src/client.cpp)
target_include_directories(client BEFORE PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_add_protobuf(client example.proto)
target_add_protobuf(client health.proto)
target_link_libraries(client PUBLIC asan Boost::asio ssl crypto asio-grpc spdlog::spdlog
                                    pthread dl)
