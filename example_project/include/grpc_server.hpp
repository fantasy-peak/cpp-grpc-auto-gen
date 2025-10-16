/**
 *
 *  @file grpc_server.hpp
 *  @author fantasy-peak
 *  Auto generate by https://github.com/fantasy-peak/cpp-grpc-auto-gen.git
 *  Copyright 2024, fantasy-peak. All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 */

#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#ifdef AGRPC_BOOST_ASIO
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#else
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <system/detail/error_code.hpp>
#endif

#include <example.grpc.pb.h>
#include <example.pb.h>
#include <health.grpc.pb.h>
#include <health.pb.h>

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#ifdef OPEN_GRPC_REFLECTION_PLUGIN
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#endif
#include <agrpc/asio_grpc.hpp>
#include <agrpc/health_check_service.hpp>

namespace peak {

#ifdef AGRPC_BOOST_ASIO
namespace asio = boost::asio;
#endif

enum class LogLevel : uint8_t {
    Info,
    Error,
};

consteval std::string_view extractFilename(const char* path) {
    std::string_view path_view{path};
    std::size_t last_slash = path_view.find_last_of("/\\");
    return (last_slash == std::string_view::npos)
               ? path_view
               : path_view.substr(last_slash + 1);
}

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

struct GrpcConfig {
    std::string host;
    int32_t thread_count{2};
    int32_t keepalive_time_ms{10000};
    int32_t keepalive_timeout_ms{10000};
    int32_t keepalive_permit_without_calls{1};
    int32_t http2_max_pings_without_data{0};
    int32_t http2_min_sent_ping_interval_without_data_ms{10000};
    int32_t http2_min_recv_ping_interval_without_data_ms{5000};
    std::optional<int32_t> min_pollers;
    std::optional<int32_t> max_pollers;
};

template <auto RequestRPC>
using AwaitableServerRPC =
    asio::use_awaitable_t<>::as_default_on_t<agrpc::ServerRPC<RequestRPC>>;

using ExampleService = fantasy::v1::Example::AsyncService;
using ExampleNoticeRPC = AwaitableServerRPC<&ExampleService::RequestNotice>;
using ExampleOrderRPC = AwaitableServerRPC<&ExampleService::RequestOrder>;
using ExampleGetOrderSeqNoRPC =
    AwaitableServerRPC<&ExampleService::RequestGetOrderSeqNo>;
using ExampleServerStreamingRPC =
    AwaitableServerRPC<&ExampleService::RequestServerStreaming>;
using ExampleClientStreamingRPC =
    AwaitableServerRPC<&ExampleService::RequestClientStreaming>;
#ifdef AGRPC_BOOST_ASIO
template <typename T>
using ConcurrentChannel =
    asio::experimental::concurrent_channel<void(boost::system::error_code, T)>;
#else
template <typename T>
using ConcurrentChannel =
    asio::experimental::concurrent_channel<void(asio::error_code, T)>;
#endif

/*************************************************** */
#if USE_GRPC_NOTIFY_WHEN_DONE
struct ServerRPCNotifyWhenDoneTraits : agrpc::DefaultServerRPCTraits {
    static constexpr bool NOTIFY_WHEN_DONE = true;
};

template <auto RequestRPC>
using NotifyWhenDoneServerRPC =
    agrpc::ServerRPC<RequestRPC, ServerRPCNotifyWhenDoneTraits>;

using ExampleServerStreamingNotifyWhenDoneRPC =
    NotifyWhenDoneServerRPC<&ExampleService::RequestServerStreaming>;

#endif
/*************************************************** */

class GrpcServer final {
  public:
    GrpcServer(GrpcConfig config) : m_config(std::move(config)) {
    }

    ~GrpcServer() = default;

    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;
    GrpcServer(GrpcServer&&) = delete;
    GrpcServer& operator=(GrpcServer&&) = delete;

    static auto create(GrpcConfig config) {
        return std::make_unique<GrpcServer>(std::move(config));
    }

    void start() {
        checkCallback();
        grpc::ServerBuilder builder;
        for (int i = 0; i < m_config.thread_count; i++) {
            m_grpc_contexts.emplace_back(std::make_shared<agrpc::GrpcContext>(
                builder.AddCompletionQueue()));
        }
        auto creds = grpc::InsecureServerCredentials();
        if (m_create_server_credentials)
            creds = m_create_server_credentials();
        builder.AddListeningPort(m_config.host, creds);
#ifdef OPEN_GRPC_REFLECTION_PLUGIN
        m_log(LogLevel::Info,
              extractFilename(__FILE__),
              __LINE__,
              "call InitProtoReflectionServerBuilderPlugin");
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
#endif
        m_example_service = std::make_unique<ExampleService>();
        builder.RegisterService(m_example_service.get());

        // https://stackoverflow.com/questions/64297617/grpc-c-how-to-detect-client-disconnected-in-async-server
        grpc::ChannelArguments args;
        // keepalive
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS,
                                   m_config.keepalive_time_ms);
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                                   m_config.keepalive_timeout_ms);
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS,
                                   m_config.keepalive_permit_without_calls);
        builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA,
                                   m_config.http2_max_pings_without_data);
        builder.AddChannelArgument(
            GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS,
            m_config.http2_min_sent_ping_interval_without_data_ms);
        builder.AddChannelArgument(
            GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
            m_config.http2_min_recv_ping_interval_without_data_ms);
        if (m_add_channel_argument)
            m_add_channel_argument(builder);

        if (m_config.min_pollers.has_value())
            builder.SetSyncServerOption(grpc::ServerBuilder::MIN_POLLERS,
                                        m_config.min_pollers.value());
        if (m_config.max_pollers.has_value())
            builder.SetSyncServerOption(grpc::ServerBuilder::MAX_POLLERS,
                                        m_config.max_pollers.value());

        agrpc::add_health_check_service(builder);
        m_server_ptr = builder.BuildAndStart();
        agrpc::start_health_check_service(*m_server_ptr, *m_grpc_contexts[0]);

        for (int32_t i = 0; i < m_config.thread_count; ++i) {
            m_threads.emplace_back([this, i] {
                auto& grpc_context = *m_grpc_contexts[i];
                registerHandler(grpc_context);
                grpc_context.run();
            });
        }
    }

    void stop() {
        if (m_grpc_contexts.empty())
            return;
        asio::post(*m_grpc_contexts[0], [&] { m_server_ptr->Shutdown(); });
        for (auto& thread : m_threads)
            thread.join();
        m_grpc_contexts.clear();
        m_threads.clear();
    }

    void setCreateSslServerCredentialsCallback(auto cb) {
        m_create_server_credentials = std::move(cb);
    }

    void setAddChannelArgumentCallback(auto cb) {
        m_add_channel_argument = std::move(cb);
    }

    void setLogCallback(auto cb) {
        m_log = std::move(cb);
    }

    void setExampleNoticeRpcCallback(auto cb) {
        m_example_notice_rpc_handler = std::move(cb);
    }

    void setExampleOrderRpcCallback(auto cb) {
        m_example_order_rpc_handler = std::move(cb);
    }

    void setExampleGetOrderSeqNoRpcCallback(auto cb) {
        m_example_get_order_seq_no_rpc_handler = std::move(cb);
    }

    void setExampleServerStreamingRpcCallback(auto cb) {
        m_example_server_streaming_rpc_handler = std::move(cb);
    }

    void setExampleClientStreamingRpcCallback(auto cb) {
        m_example_client_streaming_rpc_handler = std::move(cb);
    }

#if USE_GRPC_NOTIFY_WHEN_DONE
    void setExampleServerStreamingNotifyWhenDoneRpcCallback(auto cb) {
        m_example_server_streaming_notify_when_done_rpc_handler = std::move(cb);
    }
#endif

  private:
    void checkCallback() {
#if USE_GRPC_NOTIFY_WHEN_DONE
        if (!m_example_notice_rpc_handler)
            throw std::runtime_error("not call setExampleNoticeRpcCallback");
        if (!m_example_order_rpc_handler)
            throw std::runtime_error("not call setExampleOrderRpcCallback");
        if (!m_example_get_order_seq_no_rpc_handler)
            throw std::runtime_error(
                "not call setExampleGetOrderSeqNoRpcCallback");
        if (!m_example_server_streaming_notify_when_done_rpc_handler)
            throw std::runtime_error(
                "not call setExampleServerStreamingNotifyWhenDoneRpcCallback");
        if (!m_example_client_streaming_rpc_handler)
            throw std::runtime_error(
                "not call setExampleClientStreamingRpcCallback");
#else
        if (!m_example_notice_rpc_handler)
            throw std::runtime_error("not call setExampleNoticeRpcCallback");
        if (!m_example_order_rpc_handler)
            throw std::runtime_error("not call setExampleOrderRpcCallback");
        if (!m_example_get_order_seq_no_rpc_handler)
            throw std::runtime_error(
                "not call setExampleGetOrderSeqNoRpcCallback");
        if (!m_example_server_streaming_rpc_handler)
            throw std::runtime_error(
                "not call setExampleServerStreamingRpcCallback");
        if (!m_example_client_streaming_rpc_handler)
            throw std::runtime_error(
                "not call setExampleClientStreamingRpcCallback");
#endif
    }

    void registerHandler(auto& grpc_context) {
#if USE_GRPC_NOTIFY_WHEN_DONE
        agrpc::register_awaitable_rpc_handler<ExampleNoticeRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleNoticeRPC& rpc) -> asio::awaitable<void> {
                try {
                    co_await m_example_notice_rpc_handler(rpc);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleOrderRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleOrderRPC& rpc, fantasy::v1::OrderRequest& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_order_rpc_handler(rpc, request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleGetOrderSeqNoRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleGetOrderSeqNoRPC& rpc,
                   fantasy::v1::GetOrderSeqNoRequest& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_get_order_seq_no_rpc_handler(rpc,
                                                                    request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<
            ExampleServerStreamingNotifyWhenDoneRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleServerStreamingNotifyWhenDoneRPC& rpc,
                   ExampleServerStreamingNotifyWhenDoneRPC::Request& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_server_streaming_notify_when_done_rpc_handler(
                        rpc, request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleClientStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleClientStreamingRPC& rpc) -> asio::awaitable<void> {
                try {
                    co_await m_example_client_streaming_rpc_handler(rpc);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});
#else

        agrpc::register_awaitable_rpc_handler<ExampleNoticeRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleNoticeRPC& rpc) -> asio::awaitable<void> {
                try {
                    co_await m_example_notice_rpc_handler(rpc);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleOrderRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleOrderRPC& rpc, fantasy::v1::OrderRequest& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_order_rpc_handler(rpc, request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleGetOrderSeqNoRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleGetOrderSeqNoRPC& rpc,
                   fantasy::v1::GetOrderSeqNoRequest& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_get_order_seq_no_rpc_handler(rpc,
                                                                    request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleServerStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleServerStreamingRPC& rpc,
                   fantasy::v1::OrderRequest& request)
                -> asio::awaitable<void> {
                try {
                    co_await m_example_server_streaming_rpc_handler(rpc,
                                                                    request);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleClientStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleClientStreamingRPC& rpc) -> asio::awaitable<void> {
                try {
                    co_await m_example_client_streaming_rpc_handler(rpc);
                } catch (const std::exception& e) {
                    m_log(LogLevel::Error,
                          extractFilename(__FILE__),
                          __LINE__,
                          e.what());
                }
                co_return;
            },
            RethrowFirstArg{});

#endif
    }

    GrpcConfig m_config;
    std::function<void(LogLevel, std::string_view, int, std::string)> m_log =
        [](auto, auto, auto, auto) {};
    std::function<void(grpc::ServerBuilder&)> m_add_channel_argument;

    std::function<asio::awaitable<void>(ExampleNoticeRPC&)>
        m_example_notice_rpc_handler;
    std::function<asio::awaitable<void>(ExampleOrderRPC&,
                                        fantasy::v1::OrderRequest&)>
        m_example_order_rpc_handler;
    std::function<asio::awaitable<void>(ExampleGetOrderSeqNoRPC&,
                                        fantasy::v1::GetOrderSeqNoRequest&)>
        m_example_get_order_seq_no_rpc_handler;
    std::function<asio::awaitable<void>(ExampleServerStreamingRPC&,
                                        fantasy::v1::OrderRequest&)>
        m_example_server_streaming_rpc_handler;

    std::function<asio::awaitable<void>(ExampleClientStreamingRPC&)>
        m_example_client_streaming_rpc_handler;
#if USE_GRPC_NOTIFY_WHEN_DONE

    std::function<asio::awaitable<void>(
        ExampleServerStreamingNotifyWhenDoneRPC&,
        ExampleServerStreamingNotifyWhenDoneRPC::Request&)>
        m_example_server_streaming_notify_when_done_rpc_handler;

#endif

    std::function<std::shared_ptr<grpc::ServerCredentials>()>
        m_create_server_credentials;
    std::unique_ptr<ExampleService> m_example_service;
    std::unique_ptr<grpc::Server> m_server_ptr;
    std::vector<std::shared_ptr<agrpc::GrpcContext>> m_grpc_contexts;
    std::vector<std::thread> m_threads;
};

}  // namespace peak

/* example

#ifndef AGRPC_BOOST_ASIO
#define AGRPC_BOOST_ASIO 1
#endif

#ifndef USE_BOOST_CIRCULAR_BUFFER
#define USE_BOOST_CIRCULAR_BUFFER 1
#endif

#include <grpc_server.hpp>

int main() {
    peak::GrpcConfig config{
        .host = "0.0.0.0:5566",
        .thread_count = 1,
        .keepalive_time_ms = 10000,
        .keepalive_timeout_ms = 10000,
        .keepalive_permit_without_calls = 1,
        .http2_max_pings_without_data = 0,
        .http2_min_sent_ping_interval_without_data_ms = 10000,
        .http2_min_recv_ping_interval_without_data_ms = 5000,
        .min_pollers = 2,
        .max_pollers = 4,
    };
    // auto m_grpc_server = peak::GrpcServer::create(m_config);
    auto m_grpc_server = std::make_unique<peak::GrpcServer>(config);
    namespace asio = boost::asio;

    m_grpc_server->setExampleNoticeRpcCallback(
        [] (peak::ExampleNoticeRPC& rpc) -> asio::awaitable<void> {
            (void)rpc;
            co_return;
        });
    m_grpc_server->setExampleOrderRpcCallback(
        [] (peak::ExampleOrderRPC& rpc, fantasy::v1::OrderRequest& request) ->
asio::awaitable<void> { (void)rpc; (void)request; co_return;
        });
    m_grpc_server->setExampleGetOrderSeqNoRpcCallback(
        [] (peak::ExampleGetOrderSeqNoRPC& rpc,
fantasy::v1::GetOrderSeqNoRequest& request) -> asio::awaitable<void> {
            (void)rpc;
            (void)request;
            co_return;
        });
    m_grpc_server->setExampleServerStreamingRpcCallback(
        [] (peak::ExampleServerStreamingRPC& rpc, fantasy::v1::OrderRequest&
request) -> asio::awaitable<void> { (void)rpc; (void)request; co_return;
        });
    m_grpc_server->setExampleClientStreamingRpcCallback(
        [] (peak::ExampleClientStreamingRPC& rpc) -> asio::awaitable<void> {
            (void)rpc;
            co_return;
        });
    m_grpc_server->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    m_grpc_server->stop();

    return 0;
}

//-----------------------------------------------------------

asio::awaitable<void> notice(peak::ExampleNoticeRPC& rpc){
    (void)rpc;
    co_return;
}
m_grpc_server->setExampleNoticeRpcCallback(std::bind_front(&Test::notice,
this));


asio::awaitable<void> order(peak::ExampleOrderRPC& rpc,
fantasy::v1::OrderRequest& request) { (void)rpc; co_return;
}
m_grpc_server->setExampleOrderRpcCallback(std::bind_front(&Test::order, this));



asio::awaitable<void> getOrderSeqNo(peak::ExampleGetOrderSeqNoRPC& rpc,
fantasy::v1::GetOrderSeqNoRequest& request) { (void)rpc; co_return;
}
m_grpc_server->setExampleGetOrderSeqNoRpcCallback(std::bind_front(&Test::getOrderSeqNo,
this));



asio::awaitable<void> serverStreaming(peak::ExampleServerStreamingRPC& rpc,
fantasy::v1::OrderRequest& request) { (void)rpc; co_return;
}
m_grpc_server->setExampleServerStreamingRpcCallback(std::bind_front(&Test::serverStreaming,
this));



asio::awaitable<void> clientStreaming(peak::ExampleClientStreamingRPC& rpc){
    (void)rpc;
    co_return;
}
m_grpc_server->setExampleClientStreamingRpcCallback(std::bind_front(&Test::clientStreaming,
this));

*/