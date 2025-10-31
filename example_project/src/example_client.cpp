/**
 *
 *  @author fantasy-peak
 *  Auto generate by https://github.com/fantasy-peak/cpp-grpc-auto-gen.git
 *  Copyright 2024, fantasy-peak. All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 */

#include <thread>

#include <spdlog/spdlog.h>
#include <grpc_client.hpp>

namespace peak {

asio::awaitable<void> makeNoticeRequest(agrpc::GrpcContext& grpc_context,
                                        fantasy::v1::Example::Stub& stub) {
    // bidirectional-streaming-rpc
    using RPC =
        agrpc::ClientRPC<&fantasy::v1::Example::Stub::PrepareAsyncNotice>;

    RPC rpc{grpc_context};
    rpc.context().set_deadline(std::chrono::system_clock::now() +
                               std::chrono::seconds(5));

    if (!co_await rpc.start(stub)) {
        // Channel is either permanently broken or transiently broken but with
        // the fail-fast option.
        co_return;
    }

    // Perform a request/response ping-pong.
    fantasy::v1::NoticeRequest request;
    fantasy::v1::NoticeResponse response;

    // Reads and writes can be performed simultaneously.
    using namespace asio::experimental::awaitable_operators;
    auto [read_ok, write_ok] =
        co_await (rpc.read(response, asio::use_awaitable) &&
                  rpc.write(request, asio::use_awaitable));

    const grpc::Status status = co_await rpc.finish();
}

asio::awaitable<void> makeGetOrderSeqNoRequest(
    agrpc::GrpcContext& grpc_context,
    fantasy::v1::Example::Stub& stub) {
    using RPC = agrpc::ClientRPC<
        &fantasy::v1::Example::Stub::PrepareAsyncGetOrderSeqNo>;
    grpc::ClientContext client_context;
    client_context.set_deadline(std::chrono::system_clock::now() +
                                std::chrono::seconds(5));
    fantasy::v1::GetOrderSeqNoRequest request;
    fantasy::v1::GetOrderSeqNoResponse response;
    const auto status = co_await RPC::request(
        grpc_context, stub, client_context, request, response);
    co_return;
}

asio::awaitable<void> makeOrderRequest(agrpc::GrpcContext& grpc_context,
                                       fantasy::v1::Example::Stub& stub) {
    using RPC =
        agrpc::ClientRPC<&fantasy::v1::Example::Stub::PrepareAsyncOrder>;
    grpc::ClientContext client_context;
    client_context.set_deadline(std::chrono::system_clock::now() +
                                std::chrono::seconds(5));
    fantasy::v1::OrderRequest request;
    fantasy::v1::OrderResponse response;
    const auto status = co_await RPC::request(
        grpc_context, stub, client_context, request, response);
    co_return;
}

asio::awaitable<void> makeServerStreamingRequest(
    agrpc::GrpcContext& grpc_context,
    fantasy::v1::Example::Stub& stub) {
    using RPC = agrpc::ClientRPC<
        &fantasy::v1::Example::Stub::PrepareAsyncServerStreaming>;
    grpc::ClientContext client_context;
    client_context.set_deadline(std::chrono::system_clock::now() +
                                std::chrono::seconds(5));

    RPC rpc{grpc_context};
    rpc.context().set_deadline(std::chrono::system_clock::now() +
                               std::chrono::seconds(5));

    fantasy::v1::OrderRequest request;
    co_await rpc.start(stub, request);

    fantasy::v1::OrderResponse response;

    while (co_await rpc.read(response)) {
        spdlog::info("ClientRPC: Recv Server streaming ");
    }

    const grpc::Status status = co_await rpc.finish();

    co_return;
}

asio::awaitable<void> makeClientStreamingRequest(
    agrpc::GrpcContext& grpc_context,
    fantasy::v1::Example::Stub& stub) {
    using RPC = agrpc::ClientRPC<
        &fantasy::v1::Example::Stub::PrepareAsyncClientStreaming>;
    RPC rpc{grpc_context};
    rpc.context().set_deadline(std::chrono::system_clock::now() +
                               std::chrono::seconds(5));

    fantasy::v1::OrderResponse response;
    const bool start_ok = co_await rpc.start(stub, response);

    // Optionally read initial metadata first. Otherwise it will be read along
    // with the first write.
    const bool read_ok = co_await rpc.read_initial_metadata();

    // Send a message.
    fantasy::v1::OrderRequest request;
    const bool write_ok = co_await rpc.write(request);

    // Wait for the server to recieve all our messages and obtain the server's
    // response + status.
    const grpc::Status status = co_await rpc.finish();
    auto ok = status.ok();

    spdlog::info("ClientRPC: Client streaming completed. Response");

    co_return;
}

}  // namespace peak

int main(int argc, const char** argv) {
    const char* port = argc >= 2 ? argv[1] : "5566";
    const auto host = std::string("localhost:") + port;
    const auto thread_count = std::thread::hardware_concurrency();

    peak::GrpcClient client{host, thread_count};
    client.setLogCallback(
        [](std::string_view file, int line, std::string_view msg) {
            spdlog::info("file: {}, line: {}, msg: {}", file, line, msg);
        });
    client.notice(peak::makeNoticeRequest);
    client.getOrderSeqNo(peak::makeGetOrderSeqNoRequest);
    client.order(peak::makeOrderRequest);
    client.serverStreaming(peak::makeServerStreamingRequest);
    client.clientStreaming(peak::makeClientStreamingRequest);

    return 0;
}