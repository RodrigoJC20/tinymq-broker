#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "packet.h"
#include "db_manager.h"

namespace tinymq
{

    class Session;

    class Broker
    {
    public:
        Broker(uint16_t port = 1505, size_t thread_pool_size = 4, const std::string &db_connection_str = "");

        ~Broker();

        void start();
        void stop();

        void register_session(std::shared_ptr<Session> session);
        void remove_session(std::shared_ptr<Session> session);
        bool set_topic_publish(const std::string &topic_name, const std::string &owner_client_id, bool publish);

        void subscribe(std::shared_ptr<Session> session, const std::string &topic);
        void unsubscribe(std::shared_ptr<Session> session, const std::string &topic);
        void publish(const std::string &topic, const std::vector<uint8_t> &message);
        void send_published_topics(std::shared_ptr<Session> session);

        // Check if a DbManager is configured
        bool has_database() const { return db_manager_ != nullptr; }

    private:
        void accept_connections();

        // Helper to extract client ID from topic name
        std::string extract_client_id_from_topic(const std::string &topic);
        void notify_admin_request(const std::string &owner_id, const std::string &topic_name, const std::string &requester_id);

        using TopicSubscribers = std::unordered_map<std::string, std::vector<std::shared_ptr<Session>>>;

        boost::asio::io_context io_context_;
        boost::asio::ip::tcp::acceptor acceptor_;
        size_t thread_pool_size_;
        std::vector<std::thread> threads_;
        std::mutex sessions_mutex_;
        std::mutex topics_mutex_;
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions_; // client_id -> session
        TopicSubscribers topic_subscribers_;
        bool running_;

        // Database connectivity
        std::unique_ptr<DbManager> db_manager_;
        std::unordered_map<std::string, int> topic_id_cache_; // topic name -> DB topic ID
        std::mutex topic_cache_mutex_;
    };

} // namespace tinymq