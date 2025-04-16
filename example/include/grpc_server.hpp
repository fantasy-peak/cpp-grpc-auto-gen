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
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <agrpc/asio_grpc.hpp>
#include <agrpc/health_check_service.hpp>

namespace peak {

#ifdef AGRPC_BOOST_ASIO
namespace asio = boost::asio;
#endif

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

template <typename T>
struct Message {
    std::shared_ptr<T> info_ptr;
    uint64_t seq_no;
};

template <typename T>
class Topic final : public std::enable_shared_from_this<Topic<T>> {
  public:
#ifdef USE_BOOST_CIRCULAR_BUFFER
    explicit Topic(const std::string& topic, std::size_t count)
        : m_topic(topic), m_buffer(count) {
    }
#else
    explicit Topic(const std::string& topic, std::size_t count)
        : m_topic(topic) {
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
        ScopedConn(std::weak_ptr<Topic<T>> topic_ptr, uint64_t call_id)
            : m_topic_ptr(std::move(topic_ptr)), m_call_id(call_id) {
        }

        ScopedConn(const ScopedConn&) = delete;
        ScopedConn(ScopedConn&&) = delete;
        ScopedConn& operator=(const ScopedConn&) = delete;
        ScopedConn& operator=(ScopedConn&&) = delete;

        ~ScopedConn() {
            if (m_reset)
                return;
            if (auto sp = m_topic_ptr.lock())
                sp->erase(m_call_id);
        }

        auto callId() {
            return m_call_id;
        }

        auto reset() {
            m_reset = true;
        }

        std::weak_ptr<Topic<T>> m_topic_ptr;
        uint64_t m_call_id;
        bool m_reset{false};
    };

    void setNoticeCallback(auto& cb) {
        std::lock_guard lk(m_mutex);
        m_notice_cb = cb;
    }

    void recover(const std::vector<std::shared_ptr<Message<T>>>& vec) {
        std::lock_guard lk(m_mutex);
        if (vec.empty())
            return;
        m_buffer.insert(m_buffer.end(), vec.begin(), vec.end());
        m_seq_no = vec.back()->seq_no;
        m_seq_no++;
    }

    void publish(std::shared_ptr<T> ptr) {
        std::lock_guard lk(m_mutex);
        auto message_ptr =
            std::make_shared<Message<T>>(std::move(ptr), m_seq_no++);
        m_buffer.push_back(std::move(message_ptr));
        auto& buffer_ptr = m_buffer.back();
        if (m_notice_cb)
            m_notice_cb(m_topic, buffer_ptr);
        for (auto& [call_id, sig] : m_sig)
            sig(buffer_ptr);
    }

    auto subscribe(uint64_t seq_no,
                   std::function<void(const std::shared_ptr<Message<T>>&)> cb) {
        std::lock_guard lk(m_mutex);
        return findAndSubscribe(seq_no, std::move(cb));
    }

    auto resubscribe(
        std::optional<uint64_t> call_id,
        uint64_t seq_no,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb) {
        std::lock_guard lk(m_mutex);
        if (call_id.has_value())
            m_sig.erase(call_id.value());
        return findAndSubscribe(seq_no, std::move(cb));
    }

#ifdef USE_BOOST_CIRCULAR_BUFFER
    auto subscribe(
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        const std::function<std::vector<std::shared_ptr<Message<T>>>(
            const boost::circular_buffer<std::shared_ptr<Message<T>>>&)>&
            filter) {
#else
    auto subscribe(
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        const std::function<std::vector<std::shared_ptr<Message<T>>>(
            const std::vector<std::shared_ptr<Message<T>>>&)>& filter) {
#endif
        std::lock_guard lk(m_mutex);
        auto scoped_connection_ptr =
            std::make_unique<ScopedConn>(this->shared_from_this(), m_call_id);
        m_sig.emplace(m_call_id++, std::move(cb));
        auto msg_vec = filter(m_buffer);
        return std::make_tuple(std::move(msg_vec),
                               std::move(scoped_connection_ptr));
    }

#ifdef USE_BOOST_CIRCULAR_BUFFER
    auto resubscribe(
        std::optional<uint64_t> call_id,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        const std::function<std::vector<std::shared_ptr<Message<T>>>(
            const boost::circular_buffer<std::shared_ptr<Message<T>>>&)>&
            filter) {
#else
    auto resubscribe(
        std::optional<uint64_t> call_id,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        const std::function<std::vector<std::shared_ptr<Message<T>>>(
            const std::vector<std::shared_ptr<Message<T>>>&)>& filter) {
#endif
        std::lock_guard lk(m_mutex);
        if (call_id.has_value())
            m_sig.erase(call_id.value());
        auto scoped_connection_ptr =
            std::make_unique<ScopedConn>(this->shared_from_this(), m_call_id);
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
    auto findAndSubscribe(
        uint64_t seq_no,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb) {
        auto scoped_connection_ptr =
            std::make_unique<ScopedConn>(this->shared_from_this(), m_call_id);
        m_sig.emplace(m_call_id++, std::move(cb));
        std::vector<std::shared_ptr<Message<T>>> msg_vec;
        auto it = std::ranges::find_if(m_buffer, [&](auto& ptr) {
            return seq_no == ptr->seq_no;
        });
        if (it == m_buffer.end()) {
            auto& front = m_buffer.front();
            if (seq_no < front->seq_no) {
                it = m_buffer.begin();
            } else {
                return std::make_tuple(std::move(msg_vec),
                                       std::move(scoped_connection_ptr));
            }
        }
        msg_vec.reserve(std::distance(it, m_buffer.end()));
        std::for_each(it, m_buffer.end(), [&](auto& ptr) {
            msg_vec.emplace_back(ptr);
        });
        return std::make_tuple(std::move(msg_vec),
                               std::move(scoped_connection_ptr));
    }

    void erase(uint64_t call_id) {
        std::lock_guard lk(m_mutex);
        m_sig.erase(call_id);
    }

    std::string m_topic;
#ifdef USE_BOOST_CIRCULAR_BUFFER
    boost::circular_buffer<std::shared_ptr<Message<T>>> m_buffer;
#else
    std::vector<std::shared_ptr<Message<T>>> m_buffer;
#endif
    std::mutex m_mutex;
    std::unordered_map<uint64_t,
                       std::function<void(const std::shared_ptr<Message<T>>&)>>
        m_sig;
    uint64_t m_call_id{0};
    uint64_t m_seq_no{0};
    std::function<void(const std::string&, const std::shared_ptr<Message<T>>&)>
        m_notice_cb;
};

template <typename T>
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

    void setNoticeCallback(
        const std::function<void(const std::string&,
                                 const std::shared_ptr<Message<T>>&)>& cb) {
        m_notice_cb = cb;
    }

    void recover(std::function<std::unordered_map<
                     std::string,
                     std::vector<std::shared_ptr<Message<T>>>>()> cb) {
        auto data = cb();
        for (auto& [topic, vec] : data) {
            auto topic_ptr = getTopicPtr(topic);
            topic_ptr->recover(vec);
        }
    }

    decltype(auto) subscribe(
        const std::string& topic,
        uint64_t seq_no,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb) {
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->subscribe(seq_no, std::move(cb));
    }

    decltype(auto) resubscribe(
        std::optional<uint64_t> call_id,
        const std::string& topic,
        uint64_t seq_no,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb) {
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->resubscribe(std::move(call_id),
                                      seq_no,
                                      std::move(cb));
    }

#ifdef USE_BOOST_CIRCULAR_BUFFER
    decltype(auto) subscribe(
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        std::function<std::vector<std::shared_ptr<Message<T>>>(
            const boost::circular_buffer<std::shared_ptr<Message<T>>>&)>
            filter) {
#else
    decltype(auto) subscribe(
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        std::function<std::vector<std::shared_ptr<Message<T>>>(
            const std::vector<std::shared_ptr<Message<T>>>&)> filter) {
#endif
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->subscribe(std::move(cb), filter);
    }

#ifdef USE_BOOST_CIRCULAR_BUFFER
    decltype(auto) resubscribe(
        std::optional<uint64_t> call_id,
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        std::function<std::vector<std::shared_ptr<Message<T>>>(
            const boost::circular_buffer<std::shared_ptr<Message<T>>>&)>
            filter) {
#else
    decltype(auto) resubscribe(
        std::optional<uint64_t> call_id,
        const std::string& topic,
        std::function<void(const std::shared_ptr<Message<T>>&)> cb,
        std::function<std::vector<std::shared_ptr<Message<T>>>(
            const std::vector<std::shared_ptr<Message<T>>>&)> filter) {
#endif
        auto topic_ptr = getTopicPtr(topic);
        return topic_ptr->resubscribe(std::move(call_id),
                                      std::move(cb),
                                      filter);
    }

    template <typename Input>
        requires std::is_same_v<std::decay_t<Input>, T>
    void publish(const std::string& topic, Input&& in) {
        auto topic_ptr = getTopicPtr(topic);
        topic_ptr->publish(
            std::make_shared<std::decay_t<Input>>(std::forward<Input>(in)));
        return;
    }

  private:
    std::shared_ptr<Topic<T>> getTopicPtr(const std::string& topic) {
        std::shared_ptr<Topic<T>> topic_ptr;
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
        topic_ptr = std::make_shared<Topic<T>>(topic, m_size);
        if (m_notice_cb)
            topic_ptr->setNoticeCallback(m_notice_cb);
        m_topic_map[topic] = topic_ptr;
        return topic_ptr;
    }

    std::size_t m_size;
    std::unordered_map<std::string, std::shared_ptr<Topic<T>>> m_topic_map;
    mutable std::shared_mutex m_mutex;
    std::function<void(const std::string&, const std::shared_ptr<Message<T>>&)>
        m_notice_cb;
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
    std::optional<bool> open_reflection_server;
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
    asio::experimental::concurrent_channel<void(boost::system::error_code,
                                                std::shared_ptr<Message<T>>)>;
#else
template <typename T>
using ConcurrentChannel =
    asio::experimental::concurrent_channel<void(asio::error_code,
                                                std::shared_ptr<Message<T>>)>;
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

        if (m_config.open_reflection_server.has_value() &&
            m_config.open_reflection_server.value())
            grpc::reflection::InitProtoReflectionServerBuilderPlugin();

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

        auto notify_when_done_request_handler_4 = [this](agrpc::GrpcContext&
                                                             grpc_context) {
            return [this, &grpc_context](
                       ExampleServerStreamingNotifyWhenDoneRPC& rpc,
                       ExampleServerStreamingNotifyWhenDoneRPC::Request&
                           request) -> asio::awaitable<void> {
                co_await m_example_server_streaming_notify_when_done_rpc_handler(
                    rpc, request);
                co_return;
            };
        };
        agrpc::register_awaitable_rpc_handler<
            ExampleServerStreamingNotifyWhenDoneRPC>(
            grpc_context,
            *m_example_service,
            notify_when_done_request_handler_4(grpc_context),
            RethrowFirstArg{});

        agrpc::register_awaitable_rpc_handler<ExampleClientStreamingRPC>(
            grpc_context,
            *m_example_service,
            [this](ExampleClientStreamingRPC& rpc) -> asio::awaitable<void> {
                co_await m_example_client_streaming_rpc_handler(rpc);
                co_return;
            },
            RethrowFirstArg{});
#else

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

#endif
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