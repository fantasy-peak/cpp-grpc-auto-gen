// #define AGRPC_STANDALONE_ASIO
// #define AGRPC_BOOST_ASIO
#include <example.grpc.pb.h>
#include <example.pb.h>
#include <health.grpc.pb.h>
#include <health.pb.h>
#include <thread>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/io_context.hpp>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <agrpc/alarm.hpp>
#include <agrpc/asio_grpc.hpp>
#include <agrpc/client_rpc.hpp>
#include <agrpc/health_check_service.hpp>

#include <spdlog/spdlog.h>

namespace asio = boost::asio;

using ExampleStub = fantasy::v1::Example::Stub;

namespace fantasy {
template <auto PrepareAsync>
using AwaitableClientRPC =
    asio::use_awaitable_t<>::as_default_on_t<agrpc::ClientRPC<PrepareAsync>>;
}

asio::awaitable<void> make_bidirectional_streaming_request(
    agrpc::GrpcContext& grpc_context,
    ExampleStub& stub) {
    using RPC = fantasy::AwaitableClientRPC<&ExampleStub::PrepareAsyncNotice>;

    RPC rpc{grpc_context};
    rpc.context().set_deadline(std::chrono::system_clock::now() +
                               std::chrono::seconds(50));

    if (!co_await rpc.start(stub)) {
        // Channel is either permanently broken or transiently broken but with
        // the fail-fast option.
        co_return;
    }

    // Perform a request/response ping-pong.
    fantasy::v1::NoticeRequest request;
    request.set_notice_seq_no("0");
    fantasy::v1::NoticeResponse response;

    using namespace asio::experimental::awaitable_operators;
    co_await rpc.write(request);
    while (true) {
        auto read_ok = co_await rpc.read(response);
        if (!read_ok) {
            spdlog::error("read error");
            break;
        }
        spdlog::info("recv: {}", response.notice_seq_no());
    }

    const grpc::Status status = co_await rpc.finish();
    if (status.ok())
        spdlog::info("OK", response.notice_seq_no());
}

namespace fantasy {
// Using this as the completion token to functions like asio::co_spawn ensures
// that exceptions thrown by the coroutine are rethrown from grpc_context.run().
struct RethrowFirstArg {
    template <class... T>
    void operator()(std::exception_ptr ep, T&&...) {
        if (ep) {
            std::rethrow_exception(ep);
        }
    }

    template <class... T>
    void operator()(T&&...) {
    }
};
}  // namespace fantasy

int main(int argc, const char** argv) {
    const auto host = std::string("127.0.0.1:5566");

    auto channel =
        grpc::CreateChannel(host, grpc::InsecureChannelCredentials());

    ExampleStub stub{channel};
    agrpc::GrpcContext grpc_context;
    asio::io_context io_context{1};

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void> {
            co_await make_bidirectional_streaming_request(grpc_context, stub);
        },
        fantasy::RethrowFirstArg{});

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void> {
            using RPC = fantasy::AwaitableClientRPC<
                &fantasy::v1::Example::Stub::PrepareAsyncGetOrderSeqNo>;
            grpc::ClientContext client_context;
            fantasy::v1::GetOrderSeqNoRequest request;
            fantasy::v1::GetOrderSeqNoResponse response;
            auto status = co_await RPC::request(
                grpc_context, stub, client_context, request, response);
            // status.ok()
            spdlog::info("GetOrderSeqNoRequest response: {}",
                         response.order_seq_no());
            std::thread([&] {
                // https://github.com/grpc/grpc/blob/master/test/cpp/end2end/health_service_end2end_test.cc#L148
                auto stub = grpc::health::v1::Health::NewStub(channel);
                grpc::ClientContext context;
                ::grpc::health::v1::HealthCheckRequest request;
                ::grpc::health::v1::HealthCheckResponse response;

                grpc::Status status = stub->Check(&context, request, &response);
                if (status.ok()) {
                    spdlog::info(
                        "Health check ok: {}",
                        response.status() ==
                            grpc::health::v1::HealthCheckResponse::SERVING);
                } else {
                    spdlog::error("Health check failed: {}",
                                  status.error_message());
                }
            }).detach();
        },
        fantasy::RethrowFirstArg{});

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void> {
            using RPC = fantasy::AwaitableClientRPC<
                &fantasy::v1::Example::Stub::PrepareAsyncOrder>;
            grpc::ClientContext client_context;
            fantasy::v1::OrderRequest request;
            request.set_order_seq_no("2000sssssssssssss");
            fantasy::v1::OrderResponse response;
            auto status = co_await RPC::request(
                grpc_context, stub, client_context, request, response);
            spdlog::info("OrderRequest: {} {}",
                         status.ok(),
                         response.order_seq_no());
        },
        fantasy::RethrowFirstArg{});

    asio::post(io_context, [&] {
        agrpc::run(grpc_context, io_context, [&] {
            return grpc_context.is_stopped();
        });
    });
    asio::post(io_context, [&] {});
    io_context.run();

    std::this_thread::sleep_for(std::chrono::seconds(100));
}
