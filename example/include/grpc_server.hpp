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
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <shared_mutex>

#ifdef USE_BOOST_CIRCULAR_BUFFER
#include <boost/circular_buffer.hpp>
#endif
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

#include <agrpc/asio_grpc.hpp>
#include <agrpc/health_check_service.hpp>

namespace agrpc {

#ifdef AGRPC_BOOST_ASIO
namespace asio = boost::asio;
#endif

template <typename T>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

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

struct Message {
    std::shared_ptr<void> info_ptr;
    uint64_t seq_no;
};

class Topic final : public std::enable_shared_from_this<Topic> {
  public:
#ifdef USE_BOOST_CIRCULAR_BUFFER
    explicit Topic(std::size_t count) : m_buffer(count) {
    }
#else
    explicit Topic(std::size_t count) {
        m_buffer.reserve(count);
    }
#endif

    ~Topic() {
        clear();
    }

    Topic(const Topic&) = delete;
    Topic(Topic&&) = delete;
    Topic& operator=(const Topic&) = delete;
    Topic& operator=(Topic&&) = delete;

    struct ScopedConn {
        ScopedConn(std::weak_ptr<Topic> topic_ptr, uint64_t call_id)
            : m_topic_ptr(std::move(topic_ptr)), m_call_id(call_id) {
        }

        ScopedConn(const ScopedConn&) = delete;
        ScopedConn(ScopedConn&&) = delete;
        ScopedConn& operator=(const ScopedConn&) = delete;
        ScopedConn& operator=(ScopedConn&&) = delete;

        ~ScopedConn() {
            if (auto sp = m_topic_ptr.lock())
                sp->erase(m_call_id);
        }

        std::weak_ptr<Topic> m_topic_ptr;
        uint64_t m_call_id;
    };

    template <typename T>
        requires is_shared_ptr_v<T>
    void publish(T&& ptr) {
        std::lock_guard lk(m_mutex);
        auto message_ptr =
            std::make_shared<Message>(std::forward<T>(ptr), m_seq_no++);
        m_buffer.push_back(std::move(message_ptr));
        auto& buffer_ptr = m_buffer.back();
        for (auto& [call_id, sig] : m_sig)
            sig(buffer_ptr);
    }

    auto subscribe(uint64_t seq_no,
                   std::function<void(const std::shared_ptr<Message>&)> cb) {
        std::lock_guard lk(m_mutex);
        auto scoped_connection_ptr =
            std::make_unique<ScopedConn>(shared_from_this(), m_call_id);
        m_sig.emplace(m_call_id++, std::move(cb));
        std::vector<std::shared_ptr<Message>> msg_vec;
        auto it = std::ranges::find_if(m_buffer, [&](auto& ptr) {
            return seq_no == ptr->seq_no;
        });
        if (it == m_buffer.end())
            return std::make_tuple(std::move(msg_vec),
                                   std::move(scoped_connection_ptr));
        msg_vec.reserve(std::distance(it, m_buffer.end()));
        std::for_each(it, m_buffer.end(), [&](auto& ptr) {
            msg_vec.emplace_back(ptr);
        });
        return std::make_tuple(std::move(msg_vec),
                               std::move(scoped_connection_ptr));
    }
#ifdef USE_BOOST_CIRCULAR_BUFFER
    auto subscribe(
        std::function<void(const std::shared_ptr<Message>&)> cb,
        const std::function<std::vector<std::shared_ptr<Message>>(
            const boost::circular_buffer<std::shared_ptr<Message>>&)>& filter) {
#else
    auto subscribe(std::function<void(const std::shared_ptr<Message>&)> cb,
                   const std::function<std::vector<std::shared_ptr<Message>>(
                       const std::vector<std::shared_ptr<Message>>&)>& filter) {
#endif
        std::lock_guard lk(m_mutex);
        auto scoped_connection_ptr =
            std::make_unique<ScopedConn>(shared_from_this(), m_call_id);
        m_sig.emplace(m_call_id++, std::move(cb));
        auto msg_vec = filter(m_buffer);
        return std::make_tuple(std::move(msg_vec),
                               std::move(scoped_connection_ptr));
    }

    void clear() {
        std::lock_guard lk(m_mutex);
        m_sig.clear();
        m_buffer.clear();
    }

  private:
    void erase(uint64_t call_id) {
        std::lock_guard lk(m_mutex);
        m_sig.erase(call_id);
    }

#ifdef USE_BOOST_CIRCULAR_BUFFER
    boost::circular_buffer<std::shared_ptr<Message>> m_buffer;
#else
    std::vector<std::shared_ptr<Message>> m_buffer;
#endif
    std::mutex m_mutex;
    std::unordered_map<uint64_t,
                       std::function<void(const std::shared_ptr<Message>&)>>
        m_sig;
    uint64_t m_call_id{0};
    uint64_t m_seq_no{0};
};

class PubSubService final {
  public:
    explicit PubSubService(std::size_t count = 1000000) : m_size(count) {
    }

    ~PubSubService() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_topic_map.clear();
    }

    PubSubService(const PubSubService&) = delete;
    PubSubService(PubSubService&&) = delete;
    PubSubService& operator=(const PubSubService&) = delete;
    PubSubService& operator=(PubSubService&&) = delete;

    decltype(auto) subscribe(
        const std::string& topic,
        uint64_t seq_no,
        std::function<void(const std::shared_ptr<Message>&)> cb) {
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->subscribe(seq_no, std::move(cb));
    }
#ifdef USE_BOOST_CIRCULAR_BUFFER
    decltype(auto) subscribe(
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message>&)> cb,
        std::function<std::vector<std::shared_ptr<Message>>(
            const boost::circular_buffer<std::shared_ptr<Message>>&)> filter) {
#else
    decltype(auto) subscribe(
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message>&)> cb,
        std::function<std::vector<std::shared_ptr<Message>>(
            const std::vector<std::shared_ptr<Message>>&)> filter) {
#endif
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->subscribe(std::move(cb), filter);
    }

    template <typename T>
    void publish(const std::string& topic, T&& message) {
        auto topic_ptr = getTopicPtr(topic);
        topic_ptr->publish(std::forward<T>(message));
        return;
    }

  private:
    std::shared_ptr<Topic> getTopicPtr(const std::string& topic) {
        std::shared_ptr<Topic> topic_ptr = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto iter = m_topic_map.find(topic);
            if (iter != m_topic_map.end()) {
                topic_ptr = iter->second;
                return topic_ptr;
            }
        }
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto iter = m_topic_map.find(topic);
        if (iter != m_topic_map.end()) {
            topic_ptr = iter->second;
            return topic_ptr;
        }
        topic_ptr = std::make_shared<Topic>(m_size);
        m_topic_map[topic] = topic_ptr;
        return topic_ptr;
    }

    std::size_t m_size;
    std::unordered_map<std::string, std::shared_ptr<Topic>> m_topic_map;
    mutable std::shared_mutex m_mutex;
};

struct GrpcConfig {
    std::string host;
    int32_t thread_count{2};
    int32_t grpc_arg_keepalive_time_ms{10000};
    int32_t grpc_arg_keepalive_timeout_ms{10000};
    int32_t grpc_arg_keepalive_permit_without_calls{1};
    int32_t grpc_arg_http2_max_pings_without_data{0};
    int32_t grpc_arg_http2_min_sent_ping_interval_without_data_ms{10000};
    int32_t grpc_arg_http2_min_recv_ping_interval_without_data_ms{5000};
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
using ConcurrentChannel =
    asio::experimental::concurrent_channel<void(boost::system::error_code,
                                                std::shared_ptr<Message>)>;
#else
using ConcurrentChannel =
    asio::experimental::concurrent_channel<void(asio::error_code,
                                                std::shared_ptr<Message>)>;
#endif

class GrpcServer final {
  public:
    GrpcServer(GrpcConfig config) : m_config(std::move(config)) {
    }

    ~GrpcServer() = default;

    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;
    GrpcServer(GrpcServer&&) = delete;
    GrpcServer& operator=(GrpcServer&&) = delete;

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

        m_example_service = std::make_unique<ExampleService>();
        builder.RegisterService(m_example_service.get());

        // https://stackoverflow.com/questions/64297617/grpc-c-how-to-detect-client-disconnected-in-async-server
        grpc::ChannelArguments args;
        // keepalive
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS,
                                   m_config.grpc_arg_keepalive_time_ms);
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                                   m_config.grpc_arg_keepalive_timeout_ms);
        builder.AddChannelArgument(
            GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS,
            m_config.grpc_arg_keepalive_permit_without_calls);
        builder.AddChannelArgument(
            GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA,
            m_config.grpc_arg_http2_max_pings_without_data);
        builder.AddChannelArgument(
            GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS,
            m_config.grpc_arg_http2_min_sent_ping_interval_without_data_ms);
        builder.AddChannelArgument(
            GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
            m_config.grpc_arg_http2_min_recv_ping_interval_without_data_ms);

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

  private:
    void checkCallback() {
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
    }

    void registerHandler(auto& grpc_context) {
        agrpc::register_awaitable_rpc_handler<ExampleNoticeRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleNoticeRPC& rpc) -> asio::awaitable<void> {
                co_await m_example_notice_rpc_handler(rpc);
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleOrderRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleOrderRPC& rpc, fantasy::v1::OrderRequest& request)
                -> asio::awaitable<void> {
                co_await m_example_order_rpc_handler(rpc, request);
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleGetOrderSeqNoRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleGetOrderSeqNoRPC& rpc,
                   fantasy::v1::GetOrderSeqNoRequest& request)
                -> asio::awaitable<void> {
                co_await m_example_get_order_seq_no_rpc_handler(rpc, request);
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleServerStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleServerStreamingRPC& rpc,
                   fantasy::v1::OrderRequest& request)
                -> asio::awaitable<void> {
                co_await m_example_server_streaming_rpc_handler(rpc, request);
                co_return;
            },
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleClientStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleClientStreamingRPC& rpc) -> asio::awaitable<void> {
                co_await m_example_client_streaming_rpc_handler(rpc);
                co_return;
            },
            RethrowFirstArg{});
    }

    GrpcConfig m_config;

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
    std::function<std::shared_ptr<grpc::ServerCredentials>()>
        m_create_server_credentials;
    std::unique_ptr<ExampleService> m_example_service;
    std::unique_ptr<grpc::Server> m_server_ptr;
    std::vector<std::shared_ptr<agrpc::GrpcContext>> m_grpc_contexts;
    std::vector<std::thread> m_threads;
};

}  // namespace agrpc