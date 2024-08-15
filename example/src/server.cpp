/*#define AGRPC_STANDALONE_ASIO*/

#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifndef AGRPC_BOOST_ASIO
#define AGRPC_BOOST_ASIO 1
#endif
#include <grpc_server.hpp>

enum Status {
    OK,
    DUP,
};

template <typename CompletionToken>
auto checkClOrderId(const std::string& str, CompletionToken&& token) {
    return boost::asio::async_initiate<CompletionToken, void(Status)>(
        []<typename Handler>(Handler&& handler,
                             const std::string& str) mutable {
            auto handler_ptr =
                std::make_shared<Handler>(std::forward<Handler>(handler));
            std::thread([handler_ptr = std::move(handler_ptr), &str] {
                auto ex = boost::asio::get_associated_executor(*handler_ptr);
                boost::asio::post(ex,
                                  [handler_ptr = std::move(handler_ptr)] mutable
                                      -> void { (*handler_ptr)(Status::OK); });
            }).detach();
        },
        std::forward<CompletionToken>(token),
        str);
}

struct TestServer {
    TestServer() = default;
    ~TestServer() = default;

    boost::asio::awaitable<void> getOrderSeqNoRpcHandler(
        agrpc::ExampleGetOrderSeqNoRPC& rpc,
        fantasy::v1::GetOrderSeqNoRequest& request) {
        SPDLOG_INFO("GetOrderSeqNo");
        fantasy::v1::GetOrderSeqNoResponse response;
        static std::atomic_int32_t num{9999};
        auto count = num.fetch_add(1);
        auto ss = co_await checkClOrderId(std::string{"GetOrderSeqNo:"} +
                                              std::to_string(count),
                                          boost::asio::use_awaitable);
        if (ss == Status::OK)
            SPDLOG_INFO("GetOrderSeqNo done");
        response.set_order_seq_no("OK------");
        co_await rpc.finish(response, grpc::Status::OK);
        co_return;
    }

    boost::asio::awaitable<void> orderRpcHandler(
        agrpc::ExampleOrderRPC& rpc,
        fantasy::v1::OrderRequest& request) {
        SPDLOG_INFO("orderRpcHandler");
        fantasy::v1::OrderResponse response;
        response.set_order_seq_no("abc-OrderRequest");
        co_await rpc.finish(response, grpc::Status::OK);
    }

    boost::asio::awaitable<void> orderNoticeHandler(
        agrpc::ExampleNoticeRPC& rpc) {
        SPDLOG_INFO("orderNoticeHandler");

        fantasy::v1::NoticeRequest request;

        agrpc::Alarm alarm{rpc.get_executor()};

        agrpc::Waiter<void(bool)> waiter;
        waiter.initiate(agrpc::read, rpc, request);

        auto chan =
            std::make_shared<agrpc::ConcurrentChannel>(rpc.get_executor(),
                                                       m_channel_size);

        auto next_deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(2);
        std::unique_ptr<agrpc::Topic::ScopedConn> scoped_conn_ptr;

        while (true) {
            auto [completion_order,
                  read_error_code,
                  read_ok,
                  alarm_expired,
                  error_code,
                  notice_ptr] =
                co_await boost::asio::experimental::make_parallel_group(
                    waiter.wait(boost::asio::deferred),
                    alarm.wait(next_deadline, boost::asio::deferred),
                    chan->async_receive(boost::asio::deferred))
                    .async_wait(boost::asio::experimental::wait_for_one(),
                                boost::asio::use_awaitable);
            if (0 == completion_order[0])  // read completed
            {
                if (read_ok) {
                    auto no = std::atoi(request.notice_seq_no().c_str());
                    auto [notice_store, ptr] = m_pub_sub_service->subscribe(
                        "001",
                        no,
                        [=](const std::shared_ptr<agrpc::Message>& ptr) {
                            auto ret =
                                chan->try_send(boost::system::error_code{},
                                               ptr);
                            if (!ret) {
                                if (chan->is_open())
                                    SPDLOG_INFO("channel full");
                                else
                                    SPDLOG_INFO("channel close");
                            }
                        });
                    scoped_conn_ptr = std::move(ptr);
                    for (auto& notice_ptr : notice_store) {
                        fantasy::v1::NoticeResponse response;
                        auto pp = std::static_pointer_cast<std::string>(
                            notice_ptr->info_ptr);
                        response.set_notice_seq_no(*pp);
                        co_await rpc.write(response,
                                           boost::asio::use_awaitable);
                    }
                } else {
                    SPDLOG_INFO("client disconnect!!!");
                    break;
                }
                waiter.initiate(agrpc::read, rpc, request);
            } else if (1 == completion_order[0])  // alarm expired
            {
                if (m_stop.load(std::memory_order_relaxed)) {
                    SPDLOG_INFO("send close.");
                    co_await rpc.finish(grpc::Status::OK);
                    break;
                }
                next_deadline =
                    std::chrono::system_clock::now() + std::chrono::seconds(2);
            } else if (2 == completion_order[0]) {
                if (!error_code) {
                    fantasy::v1::NoticeResponse response;
                    auto pp = std::static_pointer_cast<std::string>(
                        notice_ptr->info_ptr);
                    response.set_notice_seq_no(*pp);
                    co_await rpc.write(response, boost::asio::use_awaitable);
                } else {
                    exit(1);
                }
            }
        }
        SPDLOG_INFO("close channel!!!");
        chan->close();
    }

    void init() {
        m_config = agrpc::GrpcConfig{
            .host = "0.0.0.0:5566",
            .thread_count = 2,
            .circular_buffer_size = 100000,
        };
        m_pub_sub_service = std::make_shared<agrpc::PubSubService>(
            m_config.circular_buffer_size);
        m_grpc_server = std::make_unique<agrpc::GrpcServer>(m_config);
        m_grpc_server->setExampleOrderRpcCallback(
            std::bind_front(&TestServer::orderRpcHandler, this));
        m_grpc_server->setExampleNoticeRpcCallback(
            std::bind_front(&TestServer::orderNoticeHandler, this));
        m_grpc_server->setExampleGetOrderSeqNoRpcCallback(
            std::bind_front(&TestServer::getOrderSeqNoRpcHandler, this));
        m_grpc_server->start();

        std::thread([this] {
            uint32_t count = 0;
            while (true) {
                for (int i = 0; i < 2; i++) {
                    m_pub_sub_service->publish("001",
                                               std::make_shared<std::string>(
                                                   std::string{"hello_"} +
                                                   std::to_string(count)));
                    count++;
                    // std::this_thread::sleep_for(std::chrono::microseconds(25));
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                // sleep(3);
            }
        }).detach();
    }

    void stop() {
        m_stop.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        m_grpc_server->stop();
        m_grpc_server.reset();
    }

    std::shared_ptr<agrpc::GrpcServer> m_grpc_server;
    std::shared_ptr<agrpc::PubSubService> m_pub_sub_service;
    agrpc::GrpcConfig m_config;
    std::atomic_bool m_stop{false};
    int32_t m_channel_size{1000000};
};

int main() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("server",
                                                   spdlog::sinks_init_list(
                                                       {console_sink}));
    logger->set_level(spdlog::level::trace);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [thr %t] [%s:%#] %v");

    TestServer TestServer;
    TestServer.init();
    std::this_thread::sleep_for(std::chrono::seconds(20));
    SPDLOG_INFO("start stop...");
    TestServer.stop();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    SPDLOG_INFO("--------------------end---------------");
    return 0;
}
