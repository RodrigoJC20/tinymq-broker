#pragma once

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <pqxx/pqxx>

namespace tinymq
{

    class DbManager
    {
    public:
        DbManager(const std::string &connection_string);
        ~DbManager();

        bool initialize();

        // Client operations
        bool register_client(const std::string &client_id, const std::string &ip_address, int port);
        bool log_client_disconnect(const std::string &client_id);

        // Topic operations
        bool register_topic(const std::string &topic_name, const std::string &owner_client_id);
        int get_topic_id(const std::string &topic_name);
        bool set_topic_publish(const std::string &topic_name, const std::string &owner_client_id, bool publish);
        // Añade esta línea:
        std::vector<std::pair<std::string, std::string>> get_published_topics();

        // Subscription operations
        bool add_subscription(const std::string &client_id, int topic_id);
        bool remove_subscription(const std::string &client_id, int topic_id);

        // Message logging
        bool log_message(const std::string &publisher_client_id,
                         int topic_id,
                         size_t payload_size,
                         const std::string &payload_preview);

        // Initialize the database with the schema if not already set up
        bool setup_schema();

    private:
        std::string connection_string_;
        std::mutex db_mutex_;

        bool client_exists(pqxx::work &txn, const std::string &client_id);
        bool topic_exists(pqxx::work &txn, const std::string &topic_name);
    };

} // namespace tinymq
