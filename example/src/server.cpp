/*#define AGRPC_STANDALONE_ASIO*/

#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "boost/asio/as_tuple.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/container/container_fwd.hpp"
#include "boost/system/detail/error_code.hpp"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/containers/string.hpp>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifndef AGRPC_BOOST_ASIO
#define AGRPC_BOOST_ASIO 1
#endif

#ifndef USE_BOOST_CIRCULAR_BUFFER
#define USE_BOOST_CIRCULAR_BUFFER 1
#endif

#define USE_GRPC_NOTIFY_WHEN_DONE 1

#include <grpc_server.hpp>
#include <pubsub_service.hpp>

struct Data {
    boost::container::string topic_name;
    boost::container::string data;
    uint64_t seq_no;
};

template <typename T>
using ShmemAllocator = boost::interprocess::
    allocator<T, boost::interprocess::managed_shared_memory::segment_manager>;
using MyVector = std::vector<Data, ShmemAllocator<Data>>;

enum Status {
    OK,
    DUP,
};

template <boost::asio::completion_token_for<void(Status)> CompletionToken>
auto checkClOrderId(const std::string& str, CompletionToken&& token) {
    return boost::asio::async_initiate<CompletionToken, void(Status)>(
        []<typename Handler>(Handler&& handler,
                             const std::string& str) mutable {
            std::thread([handler = std::forward<Handler>(handler),
                         &str]() mutable {
                auto ex = boost::asio::get_associated_executor(handler);
                boost::asio::post(ex,
                                  [handler = std::move(handler)]() mutable
                                      -> void { handler(Status::OK); });
            }).detach();
        },
        std::forward<CompletionToken>(token),
        str);
}

struct TestServer {
    using StopChannel = boost::asio::experimental::concurrent_channel<
        void(boost::system::error_code, std::string)>;

    TestServer() = default;
    ~TestServer() = default;

    boost::asio::awaitable<void> getOrderSeqNoRpcHandler(
        peak::ExampleGetOrderSeqNoRPC& rpc,
        fantasy::v1::GetOrderSeqNoRequest& /* request */) {
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
        peak::ExampleOrderRPC& rpc,
        fantasy::v1::OrderRequest& /* request */) {
        SPDLOG_INFO("orderRpcHandler");
        agrpc::Alarm alarm{rpc.get_executor()};
        auto next_deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(2);
        auto wait_ok =
            co_await alarm.wait(next_deadline, boost::asio::use_awaitable);
        SPDLOG_INFO("orderRpcHandler wait_ok: {}", wait_ok);
        fantasy::v1::OrderResponse response;
        response.set_order_seq_no("abc-OrderRequest");
        co_await rpc.finish(response, grpc::Status::OK);
    }

    boost::asio::awaitable<void> orderNoticeHandler(
        peak::ExampleNoticeRPC& rpc) {
        SPDLOG_INFO("orderNoticeHandler");

        fantasy::v1::NoticeRequest request;

        agrpc::Alarm alarm{rpc.get_executor()};

        agrpc::Waiter<void(bool)> waiter;
        waiter.initiate(agrpc::read, rpc, request);

        using DATA = std::shared_ptr<Message<std::string>>;
        auto chan =
            std::make_shared<peak::ConcurrentChannel<DATA>>(rpc.get_executor(),
                                                            m_channel_size);

        auto stop_chan = std::make_shared<StopChannel>(rpc.get_executor(), 1);

        {
            std::unique_lock lk(m_stop_mutex);
            if (m_stop) {
                lk.unlock();
                co_await rpc.finish(grpc::Status::OK);
                co_return;
            }
            m_exit_chan.emplace_back(stop_chan);
        }

        auto next_deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(2);
        std::unique_ptr<Topic<std::string>::ScopedConn> scoped_conn_ptr;
        while (true) {
            auto [completion_order,
                  read_error_code,
                  read_ok,
                  alarm_expired,
                  error_code,
                  notice_ptr,
                  stop_chan_error_code,
                  stop_chan_str] =
                co_await boost::asio::experimental::make_parallel_group(
                    waiter.wait(boost::asio::deferred),
                    alarm.wait(next_deadline, boost::asio::deferred),
                    chan->async_receive(boost::asio::deferred),
                    stop_chan->async_receive(boost::asio::deferred))
                    .async_wait(boost::asio::experimental::wait_for_one(),
                                boost::asio::use_awaitable);
            if (0 == completion_order[0])  // read completed
            {
                if (read_ok) {
                    auto no = std::stoul(request.notice_seq_no());
                    std::optional<uint64_t> call_id;
                    if (scoped_conn_ptr) {
                        call_id = scoped_conn_ptr->callId();
                        // no need remove task
                        scoped_conn_ptr->reset();
                    }
#if 1
                    auto [notice_store, ptr] = m_pub_sub_service->resubscribe(
                        call_id,
                        "001",
                        no,
                        [=](const std::shared_ptr<Message<std::string>>& ptr) {
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
#else
                    std::weak_ptr<agrpc::ConcurrentChannel<std::string>> ch =
                        chan;
                    auto [notice_store, ptr] = m_pub_sub_service->subscribe(
                        "001",
                        [=](const std::shared_ptr<agrpc::Message<std::string>>&
                                ptr) {
                            if (auto sp = ch.lock()) {
                                auto ret =
                                    sp->try_send(boost::system::error_code{},
                                                 ptr);
                                if (!ret) {
                                    if (chan->is_open())
                                        SPDLOG_INFO("channel full");
                                    else
                                        SPDLOG_INFO("channel close");
                                }
                            }
                        },
                        [seq_no =
                             no](const boost::circular_buffer<std::shared_ptr<
                                     agrpc::Message<std::string>>>& buffer)
                            -> std::vector<
                                std::shared_ptr<agrpc::Message<std::string>>> {
                            SPDLOG_INFO("call filter...");
                            std::vector<
                                std::shared_ptr<agrpc::Message<std::string>>>
                                msg_vec;
                            auto it = std::ranges::find_if(
                                buffer,
                                [&](const std::shared_ptr<
                                    agrpc::Message<std::string>>& ptr) {
                                    return seq_no == ptr->seq_no;
                                });
                            if (it == buffer.end())
                                return msg_vec;
                            msg_vec.reserve(std::distance(it, buffer.end()));
                            std::for_each(it, buffer.end(), [&](auto& ptr) {
                                msg_vec.emplace_back(ptr);
                            });
                            return msg_vec;
                        });
#endif
                    scoped_conn_ptr = std::move(ptr);
                    for (auto& notice_ptr : notice_store) {
                        fantasy::v1::NoticeResponse response;
                        response.set_notice_seq_no(*notice_ptr->info_ptr);
                        co_await rpc.write(
                            response,
                            grpc::WriteOptions().set_write_through(),
                            boost::asio::use_awaitable);
#if 0
                        std::string tmp;
                        if (!response.SerializeToString(&tmp)) {
                            SPDLOG_ERROR("SerializeToString");
                            exit(1);
                        }
                        std::cout << tmp << std::endl;
                        fantasy::v1::NoticeResponse response1;
                        if (response1.ParseFromString(tmp)) {
                            SPDLOG_INFO("notice_seq_no: {}",
                                        response1.notice_seq_no());
                        } else {
                            std::cerr << "Failed to parse NoticeResponse from string\n";
                        }
#endif
                    }
                } else {
                    SPDLOG_INFO("client disconnect!!!");
                    break;
                }
                waiter.initiate(agrpc::read, rpc, request);
            } else if (1 == completion_order[0])  // alarm expired
            {
                next_deadline =
                    std::chrono::system_clock::now() + std::chrono::seconds(2);
            } else if (2 == completion_order[0]) {
                if (error_code) {
                    SPDLOG_ERROR("{}", error_code.message());
                    co_await rpc.finish(grpc::Status::OK);
                    break;
                }
                fantasy::v1::NoticeResponse response;
                response.set_notice_seq_no(*notice_ptr->info_ptr);
                co_await rpc.write(response, boost::asio::use_awaitable);
            } else if (3 == completion_order[0]) {
                // recv stop
                SPDLOG_INFO("Receive exit signal!!!");
                co_await rpc.finish(grpc::Status::OK);
                break;
            }
        }
        SPDLOG_INFO("close channel!!!");
        chan->close();
        stop_chan->close();
    }

    void init() {
        m_config = peak::GrpcConfig{
            .host = "0.0.0.0:5566",
            .thread_count = 10,
        };
        // shared_memory_object::remove("MySharedMemory");
        static boost::interprocess::managed_shared_memory segment(
            boost::interprocess::open_or_create, "MySharedMemory", 65536);

        static boost::interprocess::named_mutex mtx(
            boost::interprocess::open_or_create,
            "your_globally_unique_mutex_name");

        m_pub_sub_service =
            std::make_shared<PubSubService<std::string>>(100000);

        m_pub_sub_service->setNoticeCallback(
            [](const std::string& topic_name,
               const std::shared_ptr<Message<std::string>>& ptr) mutable {
                using namespace boost::interprocess;
                // scoped_lock<named_mutex> lock(mtx);
                // auto* myvector = segment.find_or_construct<MyVector>(
                //     "MyVector")(segment.get_segment_manager());
                // Data dd{{topic_name.data(), topic_name.size()},
                //         {ptr->info_ptr->data(), ptr->info_ptr->size()},
                //         ptr->seq_no};
                // myvector->push_back(dd);
                SPDLOG_INFO("seq_no: {}, {}", topic_name, ptr->seq_no);
            });

        m_pub_sub_service->recover([]() mutable {
            std::unordered_map<
                std::string,
                std::vector<std::shared_ptr<Message<std::string>>>>
                cache;
            using namespace boost::interprocess;
            // scoped_lock<named_mutex> lock(mtx);
            // MyVector* myvector = segment.find<MyVector>("MyVector").first;
            // if (myvector == nullptr)
            //     return cache;
            // SPDLOG_INFO("recover myvector");
            // for (auto& ptr : *myvector) {
            //     std::cout << "recover:" << ptr.seq_no << std::endl;
            //     auto p = std::make_shared<Message<std::string>>(
            //         std::make_shared<std::string>(ptr.data.begin(),
            //                                       ptr.data.end()),
            //         ptr.seq_no);
            //     cache[ptr.topic_name].emplace_back(p);
            // }
            return cache;
        });

        m_grpc_server = std::make_unique<peak::GrpcServer>(m_config);
        m_grpc_server->setLogCallback([](int line, std::string) {
            std::cout << "line:" << line << std::endl;
            exit(1);
        });
        m_grpc_server->setExampleOrderRpcCallback(
            std::bind_front(&TestServer::orderRpcHandler, this));
        m_grpc_server->setExampleNoticeRpcCallback(
            std::bind_front(&TestServer::orderNoticeHandler, this));
        m_grpc_server->setExampleGetOrderSeqNoRpcCallback(
            std::bind_front(&TestServer::getOrderSeqNoRpcHandler, this));
#if USE_GRPC_NOTIFY_WHEN_DONE
        m_grpc_server->setExampleServerStreamingNotifyWhenDoneRpcCallback(
            [](peak::ExampleServerStreamingNotifyWhenDoneRPC& rpc,
               peak::ExampleServerStreamingNotifyWhenDoneRPC::Request& request)
                -> boost::asio::awaitable<void> {
                peak::ExampleServerStreamingNotifyWhenDoneRPC::Response
                    response;

                using namespace boost::asio::experimental::awaitable_operators;
                using Channel = boost::asio::experimental::concurrent_channel<
                    void(boost::system::error_code, std::string)>;

                auto chan = std::make_shared<Channel>(rpc.get_executor(), 1000);
                /*
                spdlog::info("NotifyWhenDoneRPC----------");
                auto check = [](auto& rpc,
                                auto chan) -> boost::asio::awaitable<void> {
                    agrpc::Alarm alarm{rpc.get_executor()};
                    while (true) {
                        co_await alarm.wait(std::chrono::system_clock::now() +
                                                std::chrono::seconds(2),
                                            boost::asio::deferred);
                        spdlog::info("NotifyWhenDoneRPC: {}",
                                     rpc.context().IsCancelled());
                        if (rpc.context().IsCancelled())
                            break;
                    }
                    chan->close();
                    co_return;
                };
                auto recv = [](auto& rpc,
                               auto chan) -> boost::asio::awaitable<void> {
                    for (;;) {
                        auto [ec, str] = co_await chan->async_receive(
                            boost::asio::as_tuple(boost::asio::use_awaitable));
                        if (ec) {
                            spdlog::info("{}", ec.message());
                            break;
                        }
                    }
                    co_return;
                };
                co_await (check(rpc, chan) && recv(rpc, chan));
                */
                std::weak_ptr<Channel> ch = chan;
                std::thread([=] {
                    for (int i = 0; i < 10; i++) {
                        if (auto sp = ch.lock()) {
                            sp->try_send(boost::system::error_code{},
                                         "hello" + std::to_string(i));
                        } else {
                            return;
                        }
                        sleep(1);
                    }
                    chan->close();
                }).detach();

                while (true) {
                    auto [completion_order, channel_ok, str, ec] =
                        co_await boost::asio::experimental::make_parallel_group(
                            chan->async_receive(boost::asio::deferred),
                            rpc.wait_for_done(boost::asio::deferred))
                            .async_wait(
                                boost::asio::experimental::wait_for_one(),
                                boost::asio::use_awaitable);
                    if (completion_order[0] == 0) {
                        // channel completed, send the next message to the
                        // client:
                        if (channel_ok)
                            break;
                        spdlog::info("recv:{} {}", str, channel_ok.message());
                    } else {
                        // wait_for_done completed, IsCancelled can now be
                        // called:
                        spdlog::info("NotifyWhenDoneRPC: {}",
                                     rpc.context().IsCancelled());
                        co_return;
                    }
                }

                spdlog::info("{}", "client disconnect");
                co_return;
            });
#else
        m_grpc_server->setExampleServerStreamingRpcCallback(
            [](peak::ExampleServerStreamingRPC& rpc,
               fantasy::v1::OrderRequest& request)
                -> boost::asio::awaitable<void> {
                SPDLOG_INFO("ExampleServerStreamingRPC: {}",
                            request.order_seq_no());
                agrpc::Alarm alarm{rpc.get_executor()};
                for (int i = 0; i < 10; i++) {
                    fantasy::v1::OrderResponse response;
                    response.set_order_seq_no(
                        std::string{"ExampleServerStreamingRPC_"} +
                        std::to_string(i));
                    auto next_deadline = std::chrono::system_clock::now() +
                                         std::chrono::seconds(2);
                    auto wait_ok =
                        co_await alarm.wait(next_deadline,
                                            boost::asio::use_awaitable);
                    auto ret = co_await rpc.write(response);
                    SPDLOG_INFO("ret: {}", ret);
                    if (!ret) {
                        SPDLOG_INFO("断开");
                        break;
                    }
                }
                co_await rpc.finish(grpc::Status::OK);
                co_return;
            });
#endif
        m_grpc_server->setExampleClientStreamingRpcCallback(
            [](peak::ExampleClientStreamingRPC&)
                -> boost::asio::awaitable<void> { co_return; });
        m_grpc_server->start();

        std::weak_ptr<PubSubService<std::string>> pub_sub_service =
            m_pub_sub_service;
        std::thread([=] {
            uint32_t count = 0;
            while (true) {
                if (auto sp = pub_sub_service.lock()) {
                    for (int i = 0; i < 2; i++) {
                        auto ss = std::string{"hello_"} + std::to_string(count);
                        sp->publish("001", std::move(ss));
                        count++;
                        // std::this_thread::sleep_for(std::chrono::microseconds(25));
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                } else {
                    break;
                }
                // sleep(3);
            }
        }).detach();
    }

    void stop() {
        {
            std::lock_guard lk(m_stop_mutex);
            m_stop = true;
            for (auto& weak_ptr : m_exit_chan) {
                if (auto sp = weak_ptr.lock())
                    sp->try_send(boost::system::error_code{}, "stop");
            }
        }
        while (true) {
            {
                std::lock_guard lk(m_stop_mutex);
                std::erase_if(m_exit_chan, [](auto& chan) {
                    if (auto sp = chan.lock())
                        return false;
                    return true;
                });
                if (m_exit_chan.empty())
                    break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        SPDLOG_INFO("stop chan exit done.");
        m_grpc_server->stop();
        m_grpc_server.reset();
    }

    std::shared_ptr<peak::GrpcServer> m_grpc_server;
    std::shared_ptr<PubSubService<std::string>> m_pub_sub_service;
    peak::GrpcConfig m_config;
    bool m_stop{false};
    int32_t m_channel_size{1000000};
    std::mutex m_stop_mutex;
    std::vector<std::weak_ptr<StopChannel>> m_exit_chan;
};

int main() {
    try {
        std::string_view data{"ABCDEF"};
        std::cout << data.substr(0, 3) << '\n';
        if (data == "ABC") {
            SPDLOG_INFO("==ABC");
        }
        auto x = std::stoll("-1");
        if (x < 0) {
            SPDLOG_INFO("x < 0");
        }
        auto y = std::stoll("aaa");
    } catch (const std::invalid_argument& ex) {
        std::cout << "std::invalid_argument::what()：" << ex.what() << '\n';
    } catch (const std::out_of_range& ex) {
        std::cout << "std::out_of_range::what()：" << ex.what() << '\n';
    }

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
    std::this_thread::sleep_for(std::chrono::seconds(20));
    return 0;
}
