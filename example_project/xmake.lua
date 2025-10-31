add_rules("mode.release", "mode.debug")
set_project("grpc_example_project")
set_version("1.0.0", {build = "%Y%m%d%H%M"})
set_xmakever("2.9.8")

add_defines("AGRPC_BOOST_ASIO", "ASIO_GRPC_DISABLE_AUTOLINK", "USE_GRPC_NOTIFY_WHEN_DONE")

add_repositories("my_private_repo https://github.com/fantasy-peak/xmake-repo.git")

add_requires("boost", {configs={regex=true, asio=true}})
add_requires("abseil")
-- add_requires("cmake::Boost", {system = true, configs = {components = {"regex", "system"}}))
add_requires("spdlog", {configs={std_format=true}})
add_requires("protobuf-cpp", "protoc")
add_requires("asio-grpc f9d83e52009fda2a0c4739fdd230e14ce814f69d")
add_requires("grpc")

set_languages("c23", "c++23")
add_includedirs("include")

target("server")
    set_kind("binary")
    add_rules("protobuf.cpp")
    add_files("src/server.cpp")
    -- https://github.com/xmake-io/xmake/pull/3886
    add_files("proto/*.proto", {proto_rootdir = "proto", proto_grpc_cpp_plugin = true})

    add_packages("grpc", {order = 1})
    add_packages("protoc", {order = 2})
    add_packages("protobuf-cpp", {order = 3})
    add_packages("abseil", {order = 4})
    add_packages("spdlog", "boost", "asio-grpc", "absl")
target_end()

target("example_client")
    set_kind("binary")
    add_rules("protobuf.cpp")
    add_files("src/example_client.cpp")
    -- https://github.com/xmake-io/xmake/pull/3886
    add_files("proto/*.proto", {proto_rootdir = "proto", proto_grpc_cpp_plugin = true})

    add_packages("grpc", {order = 1})
    add_packages("protoc", {order = 2})
    add_packages("protobuf-cpp", {order = 3})
    add_packages("abseil", {order = 4})
    add_packages("spdlog", "boost", "asio-grpc", "absl")
target_end()
