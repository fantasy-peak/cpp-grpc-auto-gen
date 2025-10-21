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
    (void)rpc;
    spdlog::info("bidirectional-streaming-rpc => {}", "ExampleNotice");
    co_return;
}

asio::awaitable<void> procGetOrderSeqNo(
    peak::ExampleGetOrderSeqNoRPC& rpc,
    fantasy::v1::GetOrderSeqNoRequest& request) {
    (void)rpc;
    (void)request;
    spdlog::info("unary-rpc, procGetOrderSeqNo ExampleGetOrderSeqNo => {}",
                 toJson(request));
    co_return;
}

asio::awaitable<void> procOrder(peak::ExampleOrderRPC& rpc,
                                fantasy::v1::OrderRequest& request) {
    (void)rpc;
    (void)request;
    spdlog::info("unary-rpc, procOrder ExampleOrder => {}", toJson(request));
    co_return;
}

#if USE_GRPC_NOTIFY_WHEN_DONE
asio::awaitable<void> procServerStreaming(
    ExampleServerStreamingNotifyWhenDoneRPC& rpc,
    ExampleServerStreamingNotifyWhenDoneRPC::Request& request) {
    (void)rpc;
    (void)request;
    spdlog::info(
        "USE_GRPC_NOTIFY_WHEN_DONE server-streaming-rpc, procServerStreaming "
        "ExampleServerStreaming => {}",
        toJson(request));
    co_return;
}
#else
asio::awaitable<void> procServerStreaming(peak::ExampleServerStreamingRPC& rpc,
                                          fantasy::v1::OrderRequest& request) {
    (void)rpc;
    (void)request;
    spdlog::info(
        "server-streaming-rpc, procServerStreaming ExampleServerStreaming => "
        "{}",
        toJson(request));
    co_return;
}
#endif

asio::awaitable<void> procClientStreaming(
    peak::ExampleClientStreamingRPC& rpc) {
    (void)rpc;
    spdlog::info("client-streaming-rpc => {}", "ExampleClientStreaming");
    co_return;
}

}  // namespace peak

int main() {
    peak::GrpcConfig config{
        .host = "0.0.0.0:5566",
        .thread_count = 4,
        .keepalive_time_ms = 10000,
        .keepalive_timeout_ms = 10000,
        .keepalive_permit_without_calls = 1,
        .http2_max_pings_without_data = 0,
        .http2_min_sent_ping_interval_without_data_ms = 10000,
        .http2_min_recv_ping_interval_without_data_ms = 5000,
        .min_pollers = std::nullopt,
        .max_pollers = std::nullopt,
    };
    auto m_grpc_server = std::make_unique<peak::GrpcServer>(config);
    namespace asio = boost::asio;

    m_grpc_server->setExampleNoticeRpcCallback(
        std::bind_front(peak::procNotice));
    m_grpc_server->setExampleGetOrderSeqNoRpcCallback(
        std::bind_front(peak::procGetOrderSeqNo));
    m_grpc_server->setExampleOrderRpcCallback(std::bind_front(peak::procOrder));
#if USE_GRPC_NOTIFY_WHEN_DONE
    m_grpc_server->setExampleServerStreamingNotifyWhenDoneRpcCallback(
        std::bind_front(peak::procServerStreaming));
#else
    m_grpc_server->setExampleServerStreamingRpcCallback(
        std::bind_front(peak::procServerStreaming));
#endif
    m_grpc_server->setExampleClientStreamingRpcCallback(
        std::bind_front(peak::procClientStreaming));
    m_grpc_server->start();
    std::this_thread::sleep_for(std::chrono::seconds(5000));
    m_grpc_server->stop();

    return 0;
}