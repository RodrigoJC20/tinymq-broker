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
                void handle_admin_request(std::shared_ptr<Session> requester_session, const std::string &topic_name);
                void handle_admin_response(std::shared_ptr<Session> owner_session, const std::string &topic_name,
                                           const std::string &requester_id, bool approved);
                void send_admin_requests_list(std::shared_ptr<Session> session);

                // Check if a DbManager is configured
                bool has_database() const { return db_manager_ != nullptr; }

                void send_my_topics(std::shared_ptr<Session> session);
                void send_my_admin_requests(std::shared_ptr<Session> session);

                // NUEVOS MÉTODOS - asegúrate de que estén declarados
                void send_my_admin_topics(std::shared_ptr<Session> session);
                void handle_admin_resignation(std::shared_ptr<Session> session, const std::string &topic_name);
                void send_topic_sensors_config(std::shared_ptr<Session> session, const std::string &topic_name);

                bool set_sensor_activable(const std::string &topic_name, const std::string &sensor_name, const std::string &client_id, bool activable);

        private:
                void accept_connections();

                // Helper to extract client ID from topic name
                std::string extract_client_id_from_topic(const std::string &topic);

                // AGREGAR ESTA DECLARACIÓN QUE FALTA
                void notify_admin_resignation_to_owner(const std::string &owner_id, const std::string &topic_name, const std::string &resigned_admin_id);

                void notify_admin_request(const std::string &owner_id, const std::string &topic_name,
                                          const std::string &requester_id);
                void notify_admin_result(const std::string &requester_id, const std::string &topic_name,
                                         bool approved, const std::string &owner_id);
                void handle_my_topics_request(const Packet &packet);
                void notify_admin_revocation(const std::string &revoked_admin_id, const std::string &topic_name,
                                             const std::string &owner_id);
                void notify_admin_request_error(const std::string &requester_id,
                                                const std::string &topic_name,
                                                const std::string &error_code,
                                                const std::string &error_message);
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

                // Client activity monitoring
                std::unique_ptr<boost::asio::steady_timer> activity_timer_;
        };

} // namespace tinymq