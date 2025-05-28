#pragma once

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <pqxx/pqxx>

// Check pqxx version and define compatibility macros
#if defined(PQXX_VERSION_MAJOR) && PQXX_VERSION_MAJOR >= 7
#define PQXX_EXEC_PARAMS_DIRECT 1
#else
#define PQXX_EXEC_PARAMS_DIRECT 0
#endif

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
        std::vector<std::pair<std::string, std::string>> get_published_topics();

        // Subscription operations
        bool add_subscription(const std::string &client_id, int topic_id);
        bool remove_subscription(const std::string &client_id, int topic_id);

        // Message logging
        bool log_message(const std::string &publisher_client_id,
                         int topic_id,
                         size_t payload_size,
                         const std::string &payload_preview);

        // Check if clients are active in the database
        std::vector<std::string> get_inactive_clients(const std::vector<std::string> &client_ids);

        // Initialize the database with the schema if not already set up
        bool setup_schema();

        // Para el DbManager
        bool request_admin_status(const std::string &topic_name, const std::string &requester_id);
        bool respond_to_admin_request(const std::string &topic_name, const std::string &owner_id,
                                      const std::string &requester_id, bool approved);
        bool revoke_admin_status(const std::string &topic_name, const std::string &owner_id,
                                 const std::string &admin_id);
        bool set_sensor_status(const std::string &topic_name, const std::string &sensor_name,
                               const std::string &client_id, bool active);
        bool is_topic_admin(pqxx::work &txn, const std::string &topic_name, const std::string &client_id);

        std::vector<std::map<std::string, std::string>> get_admin_requests(const std::string &owner_id, bool only_pending = true);

        bool client_exists(const std::string &client_id);
        bool topic_exists(const std::string &topic_name);
        bool is_client_subscribed(const std::string &client_id, const std::string &topic_name);
        bool topic_has_admin(const std::string &topic_name);
        bool add_topic_admin(const std::string &topic_name, const std::string &admin_client_id);
        std::vector<std::map<std::string, std::string>> get_my_topics(const std::string &owner_id);
        std::vector<std::map<std::string, std::string>> get_client_admin_requests(const std::string &requester_id);

        std::string check_admin_request_status(const std::string &topic_name, const std::string &requester_id);

        // Nuevo método para obtener tópicos donde el cliente es administrador
        std::vector<std::map<std::string, std::string>> get_admin_topics(const std::string &admin_client_id);

        // Método para que un admin se retire voluntariamente
        bool resign_admin_status(const std::string &topic_name, const std::string &admin_client_id);

        // Método para obtener sensores de un tópico (para administradores)
        std::vector<std::map<std::string, std::string>> get_topic_sensors_config(const std::string &topic_name, const std::string &admin_client_id);

        bool set_sensor_activable(const std::string &topic_name, const std::string &sensor_name, const std::string &client_id, bool activable);
        std::string get_topic_owner(const std::string &topic_name);

    private:
        std::string connection_string_;
        std::mutex db_mutex_;

        bool client_exists(pqxx::work &txn, const std::string &client_id);
        bool topic_exists(pqxx::work &txn, const std::string &topic_name);

        template <typename... Args>
        pqxx::result exec_params(pqxx::work &txn, const std::string &query, Args &&...args)
        {
#if PQXX_EXEC_PARAMS_DIRECT
            return txn.exec_params(query, std::forward<Args>(args)...);
#else
            return txn.exec(query, pqxx::params(std::forward<Args>(args)...));
#endif
        }
    };

} // namespace tinymq
