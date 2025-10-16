/**
 *
 *  @author fantasy-peak
 *  Auto generate by https://github.com/fantasy-peak/cpp-grpc-auto-gen.git
 *  Copyright 2024, fantasy-peak. All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 */

#include <memory>
#include <thread>
#include <vector>

#include <agrpc/client_rpc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

#include <grpc_server.hpp>

namespace asio = boost::asio;

template <class Iterator>
class RoundRobin {
  public:
    RoundRobin(Iterator begin, std::size_t size) : begin_(begin), size_(size) {
    }

    decltype(auto) next() {
        const auto cur = current_.fetch_add(1, std::memory_order_relaxed);
        const auto pos = cur % size_;
        return *std::next(begin_, static_cast<std::ptrdiff_t>(pos));
    }

  private:
    Iterator begin_;
    std::size_t size_;
    std::atomic_size_t current_{0};
};

struct GuardedGrpcContext {
    agrpc::GrpcContext context;
    asio::executor_work_guard<agrpc::GrpcContext::executor_type> guard{
        context.get_executor()};
};

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
        std::cout << "ClientRPC: Recv Server streaming " << "\n";
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

    std::cout << "ClientRPC: Client streaming completed. Response " << '\n';

    co_return;
}

int main(int argc, const char** argv) {
    const char* port = argc >= 2 ? argv[1] : "5566";
    const auto host = std::string("localhost:") + port;
    const auto thread_count = std::thread::hardware_concurrency();

    fantasy::v1::Example::Stub stub(
        grpc::CreateChannel(host, grpc::InsecureChannelCredentials()));
    std::vector<std::unique_ptr<GuardedGrpcContext>> grpc_contexts;
    for (size_t i = 0; i < thread_count; ++i) {
        grpc_contexts.emplace_back(std::make_unique<GuardedGrpcContext>());
    }

    // Create one thread per GrpcContext.
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i] { grpc_contexts[i]->context.run(); });
    }

    RoundRobin round_robin_grpc_contexts{grpc_contexts.begin(), thread_count};

    {
        auto& grpc_context = round_robin_grpc_contexts.next()->context;
        asio::co_spawn(grpc_context,
                       makeNoticeRequest(grpc_context, stub),
                       peak::RethrowFirstArg{});
    }

    {
        // unary-rpc
        auto& grpc_context = round_robin_grpc_contexts.next()->context;
        asio::co_spawn(grpc_context,
                       makeOrderRequest(grpc_context, stub),
                       peak::RethrowFirstArg{});
    }

    {
        // unary-rpc
        auto& grpc_context = round_robin_grpc_contexts.next()->context;
        asio::co_spawn(grpc_context,
                       makeGetOrderSeqNoRequest(grpc_context, stub),
                       peak::RethrowFirstArg{});
    }

    {
        // server-streaming-rpc
        auto& grpc_context = round_robin_grpc_contexts.next()->context;
        asio::co_spawn(grpc_context,
                       makeServerStreamingRequest(grpc_context, stub),
                       peak::RethrowFirstArg{});
    }

    {
        // client-streaming-rpc
        auto& grpc_context = round_robin_grpc_contexts.next()->context;
        asio::co_spawn(grpc_context,
                       makeClientStreamingRequest(grpc_context, stub),
                       peak::RethrowFirstArg{});
    }

    for (auto& grpc_context : grpc_contexts) {
        grpc_context->guard.reset();
    }

    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}