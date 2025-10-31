/**
 *
 *  @author fantasy-peak
 *  Auto generate by https://github.com/fantasy-peak/cpp-grpc-auto-gen.git
 *  Copyright 2024, fantasy-peak. All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 */

#include <spdlog/spdlog.h>

#include <grpc_server.hpp>

namespace peak {

std::string toJson(auto& request) {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.always_print_fields_with_no_presence = true;
    std::string json_output;
    absl::Status ret = google::protobuf::util::MessageToJsonString(request,
                                                                   &json_output,
                                                                   options);
    return json_output;
}

asio::awaitable<void> procNotice(peak::ExampleNoticeRPC& rpc) {
    spdlog::info("bidirectional-streaming-rpc => {}", "ExampleNotice");
    fantasy::v1::NoticeRequest request;
    co_await rpc.read(request);
    fantasy::v1::NoticeResponse response;
    co_await rpc.write(response);
    co_await rpc.finish(grpc::Status::OK);
    co_return;
}

asio::awaitable<void> procGetOrderSeqNo(
    peak::ExampleGetOrderSeqNoRPC& rpc,
    fantasy::v1::GetOrderSeqNoRequest& request) {
    spdlog::info("unary-rpc, procGetOrderSeqNo ExampleGetOrderSeqNo => {}",
                 toJson(request));
    fantasy::v1::GetOrderSeqNoResponse response;
    co_await rpc.finish(response, grpc::Status::OK);
    co_return;
}

asio::awaitable<void> procOrder(peak::ExampleOrderRPC& rpc,
                                fantasy::v1::OrderRequest& request) {
    spdlog::info("unary-rpc, procOrder ExampleOrder => {}", toJson(request));
    fantasy::v1::OrderResponse response;
    co_await rpc.finish(response, grpc::Status::OK);
    co_return;
}

#if USE_GRPC_NOTIFY_WHEN_DONE
asio::awaitable<void> procServerStreaming(
    peak::ExampleServerStreamingNotifyWhenDoneRPC& rpc,
    peak::ExampleServerStreamingNotifyWhenDoneRPC::Request& request) {
    spdlog::info(
        "USE_GRPC_NOTIFY_WHEN_DONE server-streaming-rpc, procServerStreaming "
        "ExampleServerStreaming => {}",
        toJson(request));
    peak::ExampleServerStreamingNotifyWhenDoneRPC::Response response;
    if (!co_await rpc.write(response)) {
        co_return;
    }
    agrpc::Alarm alarm(rpc.get_executor());
    while (true) {
        const auto [completion_order, wait_ok, ec] =
            co_await asio::experimental::make_parallel_group(
                alarm.wait(std::chrono::system_clock::now() +
                               std::chrono::seconds(30),
                           asio::deferred),
                rpc.wait_for_done(asio::deferred))
                .async_wait(asio::experimental::wait_for_one(),
                            asio::use_awaitable);
        if (completion_order[0] == 0) {
            // alarm completed, send the next message to the client:
            if (!co_await rpc.write(response)) {
                co_return;
            }
        } else {
            // wait_for_done completed, IsCancelled can now be called:
            spdlog::info(
                "ServerRPC: Server streaming notify_when_done was successfully "
                "cancelled: {}",
                rpc.context().IsCancelled());
            co_return;
        }
    }
    co_return;
}
#else
asio::awaitable<void> procServerStreaming(peak::ExampleServerStreamingRPC& rpc,
                                          fantasy::v1::OrderRequest& request) {
    spdlog::info(
        "server-streaming-rpc, procServerStreaming ExampleServerStreaming => "
        "{}",
        toJson(request));
    fantasy::v1::OrderResponse response;
    auto ret = co_await rpc.write(response);
    co_await rpc.finish(grpc::Status::OK);
    co_return;
}
#endif

asio::awaitable<void> procClientStreaming(
    peak::ExampleClientStreamingRPC& rpc) {
    spdlog::info("client-streaming-rpc => {}", "ExampleClientStreaming");
    // Optionally send initial metadata first.
    if (!co_await rpc.send_initial_metadata()) {
        // Connection lost
        co_return;
    }

    bool read_ok;
    do {
        fantasy::v1::OrderRequest request;
        // Read from the client stream until the client has signaled
        // `writes_done`.
        read_ok = co_await rpc.read(request);
    } while (read_ok);

    fantasy::v1::OrderResponse response;
    co_await rpc.finish(response, grpc::Status::OK);
    co_return;
}

}  // namespace peak

int main() {
    peak::GrpcServerConfig config{
        .host = "0.0.0.0:5566",
        .thread_count = 4,
        .keepalive_time_ms = 10000,
        .keepalive_timeout_ms = 10000,
        .keepalive_permit_without_calls = 1,
        .http2_max_pings_without_data = 0,
        .http2_min_sent_ping_interval_without_data_ms = 10000,
        .http2_min_recv_ping_interval_without_data_ms = 5000,
        .enable_grpc_health_check = true,
    };
    auto grpc_server = peak::GrpcServer::create(config);
    grpc_server->setAddChannelArgumentCallback([](grpc::ServerBuilder&) {
        spdlog::info("setAddChannelArgumentCallback");
    });
    grpc_server->setLogCallback([](auto level, auto file, auto line, auto msg) {
        spdlog::info("level: {}, file: {}, line: {}, msg: {}",
                     level == peak::LogLevel::Info ? "info" : "error",
                     file,
                     line,
                     msg);
    });
    grpc_server->setExampleNoticeRpcCallback(std::bind_front(peak::procNotice));
    grpc_server->setExampleGetOrderSeqNoRpcCallback(
        std::bind_front(peak::procGetOrderSeqNo));
    grpc_server->setExampleOrderRpcCallback(std::bind_front(peak::procOrder));
#if USE_GRPC_NOTIFY_WHEN_DONE
    grpc_server->setExampleServerStreamingNotifyWhenDoneRpcCallback(
        std::bind_front(peak::procServerStreaming));
#else
    grpc_server->setExampleServerStreamingRpcCallback(
        std::bind_front(peak::procServerStreaming));
#endif
    grpc_server->setExampleClientStreamingRpcCallback(
        std::bind_front(peak::procClientStreaming));
    grpc_server->start();
    std::this_thread::sleep_for(std::chrono::seconds(5000));
    grpc_server->stop();

    return 0;
}