/**
 *
 *  @file grpc_client.hpp
 *  @author fantasy-peak
 *  Auto generate by https://github.com/fantasy-peak/cpp-grpc-auto-gen.git
 *  Copyright 2024, fantasy-peak. All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 */

#pragma once

#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

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

#include <agrpc/client_rpc.hpp>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

#include <example.grpc.pb.h>
#include <example.pb.h>
#include <health.grpc.pb.h>
#include <health.pb.h>

namespace peak_utils {
consteval std::string_view extractFilename(const char* path) {
    std::string_view path_view{path};
    std::size_t last_slash = path_view.find_last_of("/\\");
    return (last_slash == std::string_view::npos)
               ? path_view
               : path_view.substr(last_slash + 1);
}
}  // namespace peak_utils

namespace peak {

#ifdef AGRPC_BOOST_ASIO
namespace asio = boost::asio;
#endif

// Helper class for round-robin selection of GrpcContexts
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

// Helper struct to manage GrpcContext lifetime
struct GuardedGrpcContext {
    agrpc::GrpcContext context;
    asio::executor_work_guard<agrpc::GrpcContext::executor_type> guard{
        context.get_executor()};
};

class GrpcClient final {
  public:
    GrpcClient(const std::string& host, size_t thread_count = 4) {
        m_stub = fantasy::v1::Example::NewStub(
            grpc::CreateChannel(std::string(host),
                                grpc::InsecureChannelCredentials()));

        for (size_t i = 0; i < thread_count; ++i) {
            m_grpc_contexts.emplace_back(
                std::make_unique<GuardedGrpcContext>());
        }
        m_threads.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            m_threads.emplace_back(
                [this, i] { m_grpc_contexts[i]->context.run(); });
        }

        m_round_robin =
            std::make_unique<RoundRobin<decltype(m_grpc_contexts.begin())>>(
                m_grpc_contexts.begin(), thread_count);
    }

    ~GrpcClient() {
        stop();
    }

    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;
    GrpcClient(GrpcClient&&) = delete;
    GrpcClient& operator=(GrpcClient&&) = delete;

    static auto create(const std::string& host, size_t thread_count = 4) {
        return std::make_unique<GrpcClient>(host, thread_count);
    }

    void stop() {
        for (auto& grpc_context : m_grpc_contexts) {
            grpc_context->guard.reset();
        }

        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_threads.clear();
    }

    void setLogCallback(auto cb) {
        m_log = std::move(cb);
    }

    void notice(
        std::function<asio::awaitable<void>(agrpc::GrpcContext&,
                                            fantasy::v1::Example::Stub&)>
            handler) {
        auto& grpc_context = m_round_robin->next()->context;
        asio::co_spawn(
            grpc_context,
            [](auto& grpc_context, auto& stub, auto handler, auto self)
                -> asio::awaitable<void> {
                try {
                    co_await handler(grpc_context, stub);
                } catch (const std::exception& e) {
                    self->m_log(peak_utils::extractFilename(__FILE__),
                                __LINE__,
                                e.what());
                }
            }(grpc_context, *m_stub, std::move(handler), this),
            asio::detached);
    }

    void getOrderSeqNo(
        std::function<asio::awaitable<void>(agrpc::GrpcContext&,
                                            fantasy::v1::Example::Stub&)>
            handler) {
        auto& grpc_context = m_round_robin->next()->context;
        asio::co_spawn(
            grpc_context,
            [](auto& grpc_context, auto& stub, auto handler, auto self)
                -> asio::awaitable<void> {
                try {
                    co_await handler(grpc_context, stub);
                } catch (const std::exception& e) {
                    self->m_log(peak_utils::extractFilename(__FILE__),
                                __LINE__,
                                e.what());
                }
            }(grpc_context, *m_stub, std::move(handler), this),
            asio::detached);
    }

    void order(std::function<asio::awaitable<void>(agrpc::GrpcContext&,
                                                   fantasy::v1::Example::Stub&)>
                   handler) {
        auto& grpc_context = m_round_robin->next()->context;
        asio::co_spawn(
            grpc_context,
            [](auto& grpc_context, auto& stub, auto handler, auto self)
                -> asio::awaitable<void> {
                try {
                    co_await handler(grpc_context, stub);
                } catch (const std::exception& e) {
                    self->m_log(peak_utils::extractFilename(__FILE__),
                                __LINE__,
                                e.what());
                }
            }(grpc_context, *m_stub, std::move(handler), this),
            asio::detached);
    }

    void serverStreaming(
        std::function<asio::awaitable<void>(agrpc::GrpcContext&,
                                            fantasy::v1::Example::Stub&)>
            handler) {
        auto& grpc_context = m_round_robin->next()->context;
        asio::co_spawn(
            grpc_context,
            [](auto& grpc_context, auto& stub, auto handler, auto self)
                -> asio::awaitable<void> {
                try {
                    co_await handler(grpc_context, stub);
                } catch (const std::exception& e) {
                    self->m_log(peak_utils::extractFilename(__FILE__),
                                __LINE__,
                                e.what());
                }
            }(grpc_context, *m_stub, std::move(handler), this),
            asio::detached);
    }

    void clientStreaming(
        std::function<asio::awaitable<void>(agrpc::GrpcContext&,
                                            fantasy::v1::Example::Stub&)>
            handler) {
        auto& grpc_context = m_round_robin->next()->context;
        asio::co_spawn(
            grpc_context,
            [](auto& grpc_context, auto& stub, auto handler, auto self)
                -> asio::awaitable<void> {
                try {
                    co_await handler(grpc_context, stub);
                } catch (const std::exception& e) {
                    self->m_log(peak_utils::extractFilename(__FILE__),
                                __LINE__,
                                e.what());
                }
            }(grpc_context, *m_stub, std::move(handler), this),
            asio::detached);
    }

  private:
    std::unique_ptr<fantasy::v1::Example::Stub> m_stub;
    std::vector<std::unique_ptr<GuardedGrpcContext>> m_grpc_contexts;
    std::vector<std::thread> m_threads;
    std::unique_ptr<RoundRobin<decltype(m_grpc_contexts.begin())>>
        m_round_robin;
    std::function<void(std::string_view, int, std::string)> m_log =
        [](auto, auto, auto) {};
};
}  // namespace peak