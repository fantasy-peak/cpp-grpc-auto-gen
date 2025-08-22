#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <shared_mutex>

#ifdef USE_BOOST_CIRCULAR_BUFFER
#include <boost/circular_buffer.hpp>
#endif

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
        if (m_buffer.empty()) {
            return std::make_tuple(std::move(msg_vec),
                                   std::move(scoped_connection_ptr));
        }

        auto it = std::lower_bound(m_buffer.begin(),
                                   m_buffer.end(),
                                   seq_no,
                                   [](const auto& ptr, uint64_t value) {
                                       return ptr->seq_no < value;
                                   });
        if (it == m_buffer.end()) {
            return std::make_tuple(std::move(msg_vec),
                                   std::move(scoped_connection_ptr));
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