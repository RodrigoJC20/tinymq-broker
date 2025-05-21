#include "db_manager.h"
#include "terminal_ui.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <pqxx/pqxx>

namespace tinymq
{

    DbManager::DbManager(const std::string &connection_string)
        : connection_string_(connection_string)
    {
    }

    DbManager::~DbManager()
    {
    }

    bool DbManager::initialize()
    {
        try
        {
            // Test the connection
            pqxx::connection conn(connection_string_);
            if (conn.is_open())
            {
                ui::print_message("Database", "Successfully connected to PostgreSQL database",
                                  ui::MessageType::SUCCESS);
                return setup_schema();
            }
            else
            {
                ui::print_message("Database", "Failed to open database connection",
                                  ui::MessageType::ERROR);
                return false;
            }
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Database connection error: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::register_client(const std::string &client_id, const std::string &ip_address, int port)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            if (client_exists(txn, client_id))
            {
                // Update existing client using parameterized query
                txn.exec_params(
                    "UPDATE clients SET last_connected = CURRENT_TIMESTAMP, "
                    "last_ip = $1, last_port = $2, connection_count = connection_count + 1 "
                    "WHERE client_id = $3",
                    ip_address, port, client_id);
            }
            else
            {
                // Insert new client using parameterized query
                txn.exec_params(
                    "INSERT INTO clients (client_id, last_ip, last_port) VALUES ($1, $2, $3)",
                    client_id, ip_address, port);
            }

            // Log the connection event using parameterized query
            txn.exec_params(
                "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
                "VALUES ($1, $2, $3, $4)",
                client_id, "CONNECT", ip_address, port);

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error registering client: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::log_client_disconnect(const std::string &client_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Get the last known IP and port
            auto result = txn.exec_params(
                "SELECT last_ip, last_port FROM clients WHERE client_id = $1",
                client_id);

            if (result.empty())
            {
                ui::print_message("Database", "Client not found for disconnect: " + client_id,
                                  ui::MessageType::WARNING);
                return false;
            }

            std::string ip = result[0]["last_ip"].as<std::string>();
            int port = result[0]["last_port"].as<int>();

            // Log the disconnect event
            txn.exec_params(
                "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
                "VALUES ($1, 'DISCONNECT', $2, $3)",
                client_id, ip, port);

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error logging client disconnect: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::register_topic(const std::string &topic_name, const std::string &owner_client_id)
    {
        try
        {
            // Limpiar el formato JSON del nombre del tópico
            std::string clean_name = topic_name;

            // Si el tópico está en formato ["nombre"], eliminar los caracteres de JSON
            if (clean_name.size() >= 4 &&
                clean_name.substr(0, 2) == "[\"" &&
                clean_name.substr(clean_name.size() - 2) == "\"]")
            {
                clean_name = clean_name.substr(2, clean_name.size() - 4);
            }

            // Extraer solo el nombre del tópico (sin el client_id/)
            std::string pure_topic_name = clean_name;
            size_t pos = clean_name.find('/');
            if (pos != std::string::npos)
            {
                pure_topic_name = clean_name.substr(pos + 1);
            }

            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            if (!client_exists(txn, owner_client_id))
            {
                ui::print_message("Database", "No se puede registrar el tópico: el cliente no existe: " + owner_client_id + " (tópico: " + pure_topic_name + ")", ui::MessageType::WARNING);
                return false;
            }

            if (!topic_exists(txn, pure_topic_name))
            {
                txn.exec_params(
                    "INSERT INTO topics (name, owner_client_id) VALUES ($1, $2)",
                    pure_topic_name, owner_client_id);

                txn.commit();
                ui::print_message("Database", "Tópico registrado en la base de datos: " + pure_topic_name,
                                  ui::MessageType::SUCCESS);
            }
            else
            {
                ui::print_message("Database", "El tópico ya existe en la base de datos: " + pure_topic_name,
                                  ui::MessageType::INFO);
            }

            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error al registrar tópico: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::set_topic_publish(const std::string &topic_name, const std::string &owner_client_id, bool publish)
    {
        try
        {
            // Limpiar el formato JSON del nombre del tópico (igual que en register_topic)
            std::string clean_name = topic_name;
            if (clean_name.size() >= 4 &&
                clean_name.substr(0, 2) == "[\"" &&
                clean_name.substr(clean_name.size() - 2) == "\"]")
            {
                clean_name = clean_name.substr(2, clean_name.size() - 4);
            }

            // Extraer solo el nombre del tópico (sin el client_id/)
            std::string pure_topic_name = clean_name;
            size_t pos = clean_name.find('/');
            if (pos != std::string::npos)
            {
                pure_topic_name = clean_name.substr(pos + 1);
            }

            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar si el tópico existe y si el cliente es el propietario
            auto result = txn.exec_params(
                "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                pure_topic_name, owner_client_id);

            if (result.empty())
            {
                ui::print_message("Database",
                                  "No se puede actualizar el estado de publicación: tópico no encontrado o no es propietario",
                                  ui::MessageType::WARNING);
                return false;
            }

            // Actualizar el estado de publicación
            txn.exec_params(
                "UPDATE topics SET publish = $1 WHERE name = $2 AND owner_client_id = $3",
                publish, pure_topic_name, owner_client_id);

            txn.commit();
            ui::print_message("Database",
                              "Estado de publicación actualizado para tópico '" + pure_topic_name +
                                  "' a " + (publish ? "ACTIVO" : "INACTIVO"),
                              ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error al actualizar estado de publicación: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    int DbManager::get_topic_id(const std::string &topic_name)
    {
        try
        {
            // Limpiar el formato JSON del nombre del tópico
            std::string clean_name = topic_name;

            // Si el tópico está en formato ["nombre"], eliminar los caracteres de JSON
            if (clean_name.size() >= 4 &&
                clean_name.substr(0, 2) == "[\"" &&
                clean_name.substr(clean_name.size() - 2) == "\"]")
            {
                clean_name = clean_name.substr(2, clean_name.size() - 4);
            }

            // Extraer solo el nombre del tópico (sin el client_id/)
            std::string pure_topic_name = clean_name;
            size_t pos = clean_name.find('/');
            if (pos != std::string::npos)
            {
                pure_topic_name = clean_name.substr(pos + 1);
            }

            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = txn.exec_params(
                "SELECT id FROM topics WHERE name = $1",
                pure_topic_name);

            if (result.empty())
            {
                return -1; // Topic not found
            }

            return result[0][0].as<int>();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting topic ID: ") + e.what(),
                              ui::MessageType::ERROR);
            return -1;
        }
    }

    bool DbManager::add_subscription(const std::string &client_id, int topic_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            if (!client_exists(txn, client_id))
            {
                ui::print_message("Database", "Cannot add subscription: client does not exist: " + client_id,
                                  ui::MessageType::WARNING);
                return false;
            }

            // Check if topic exists
            auto topic_result = txn.exec_params(
                "SELECT name FROM topics WHERE id = $1",
                topic_id);

            if (topic_result.empty())
            {
                ui::print_message("Database", "Cannot add subscription: topic ID does not exist: " + std::to_string(topic_id), ui::MessageType::WARNING);
                return false;
            }

            // Check if subscription already exists
            auto sub_result = txn.exec_params(
                "SELECT id, active FROM subscriptions WHERE client_id = $1 AND topic_id = $2",
                client_id, topic_id);

            if (!sub_result.empty())
            {
                // Subscription exists - check if it's active
                bool active = sub_result[0]["active"].as<bool>();
                if (!active)
                {
                    // Reactivate it
                    txn.exec_params(
                        "UPDATE subscriptions SET active = TRUE, subscribed_at = CURRENT_TIMESTAMP "
                        "WHERE client_id = $1 AND topic_id = $2",
                        client_id, topic_id);
                }
            }
            else
            {
                // Create new subscription
                txn.exec_params(
                    "INSERT INTO subscriptions (client_id, topic_id) VALUES ($1, $2)",
                    client_id, topic_id);
            }

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error adding subscription: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::remove_subscription(const std::string &client_id, int topic_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = txn.exec_params(
                "UPDATE subscriptions SET active = FALSE "
                "WHERE client_id = $1 AND topic_id = $2",
                client_id, topic_id);

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error removing subscription: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::log_message(const std::string &publisher_client_id,
                                int topic_id,
                                size_t payload_size,
                                const std::string &payload_preview)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            txn.exec_params(
                "INSERT INTO message_logs (publisher_client_id, topic_id, payload_size, payload_preview) "
                "VALUES ($1, $2, $3, $4)",
                publisher_client_id, topic_id, static_cast<int>(payload_size), payload_preview);

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error logging message: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::client_exists(pqxx::work &txn, const std::string &client_id)
    {
        auto result = txn.exec_params(
            "SELECT 1 FROM clients WHERE client_id = $1",
            client_id);
        return !result.empty();
    }

    bool DbManager::topic_exists(pqxx::work &txn, const std::string &topic_name)
    {
        auto result = txn.exec_params(
            "SELECT 1 FROM topics WHERE name = $1",
            topic_name);
        return !result.empty();
    }

    bool DbManager::setup_schema()
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);

            // Check if our schema is already set up
            {
                pqxx::connection conn(connection_string_);
                pqxx::work txn(conn);
                auto result = txn.exec(
                    "SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'clients')");

                if (result[0][0].as<bool>())
                {
                    ui::print_message("Database", "Database schema already exists", ui::MessageType::INFO);
                    return true;
                }
                txn.commit();
            }

            // Read schema file
            std::ifstream schema_file("db/schema.sql");
            if (!schema_file.is_open())
            {
                ui::print_message("Database", "Failed to open schema file", ui::MessageType::ERROR);
                return false;
            }

            std::stringstream schema_stream;
            schema_stream << schema_file.rdbuf();
            std::string schema = schema_stream.str();

            // Execute schema in a separate connection
            {
                pqxx::connection conn(connection_string_);
                pqxx::nontransaction ntxn(conn);
                ntxn.exec(schema);
            }

            ui::print_message("Database", "Database schema successfully created", ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error setting up schema: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    std::vector<std::pair<std::string, std::string>> DbManager::get_published_topics()
    {
        std::vector<std::pair<std::string, std::string>> topics;
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = txn.exec(
                "SELECT t.name, t.owner_client_id "
                "FROM topics t "
                "WHERE t.publish = TRUE "
                "ORDER BY t.name");

            for (const auto &row : result)
            {
                std::string name = row["name"].as<std::string>();
                std::string owner = row["owner_client_id"].as<std::string>();
                topics.emplace_back(name, owner);
            }

            txn.commit();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting published topics: ") + e.what(),
                              ui::MessageType::ERROR);
        }
        return topics;
    }

    std::vector<std::map<std::string, std::string>> DbManager::get_admin_requests(const std::string &owner_id, bool only_pending)
    {
        std::vector<std::map<std::string, std::string>> requests;
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            std::string status_filter = only_pending ? " AND ar.status = 'pending'" : "";

            pqxx::result result = txn.exec_params(
                "SELECT ar.id, t.name as topic, ar.requester_client_id, ar.status, "
                "EXTRACT(EPOCH FROM ar.request_timestamp) as request_time "
                "FROM admin_requests ar "
                "JOIN topics t ON ar.topic_id = t.id "
                "WHERE t.owner_client_id = $1" +
                    status_filter + " "
                                    "ORDER BY ar.request_timestamp DESC",
                owner_id);

            for (auto row : result)
            {
                std::map<std::string, std::string> req;
                req["id"] = row["id"].as<std::string>();
                req["topic"] = row["topic"].as<std::string>();
                req["requester_id"] = row["requester_client_id"].as<std::string>();
                req["status"] = row["status"].as<std::string>();
                req["request_time"] = row["request_time"].as<std::string>();
                requests.push_back(req);
            }

            txn.commit();
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error getting admin requests: " + std::string(e.what()),
                              ui::MessageType::ERROR);
        }
        return requests;
    }

    bool DbManager::request_admin_status(const std::string &topic_name, const std::string &requester_id)
    {
        try
        {
            // Crear una conexión local como en los otros métodos
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Obtener el ID del tópico
            int topic_id = get_topic_id(topic_name);
            if (topic_id == -1)
            {
                ui::print_message("DbManager", "Topic not found: " + topic_name, ui::MessageType::WARNING);
                return false;
            }

            // Comprobar si ya existe una solicitud pendiente para este usuario y tópico
            pqxx::result check_result = txn.exec_params(
                "SELECT id FROM admin_requests "
                "WHERE topic_id = $1 AND requester_client_id = $2 AND status = 'pending'",
                topic_id, requester_id);

            if (!check_result.empty())
            {
                ui::print_message("DbManager", "Ya existe una solicitud pendiente para este usuario y tópico",
                                  ui::MessageType::INFO);
                txn.commit();
                return true; // No es un error, la solicitud ya existe
            }

            // Insertar nueva solicitud
            pqxx::result result = txn.exec_params(
                "INSERT INTO admin_requests (topic_id, requester_client_id, status, request_timestamp) "
                "VALUES ($1, $2, 'pending', NOW()) RETURNING id",
                topic_id, requester_id);

            txn.commit();

            ui::print_message("DbManager",
                              "Admin request registered for topic " + topic_name + " by " + requester_id,
                              ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error registering admin request: " + std::string(e.what()),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::respond_to_admin_request(const std::string &topic_name, const std::string &owner_id,
                                             const std::string &requester_id, bool approved)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar que el tópico existe y el dueño es correcto
            auto topic_result = txn.exec_params(
                "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                topic_name, owner_id);

            if (topic_result.empty())
            {
                ui::print_message("DbManager",
                                  "Topic not found or user is not owner: " + topic_name,
                                  ui::MessageType::WARNING);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Actualizar estado de solicitud
            std::string status = approved ? "approved" : "rejected";
            auto update_result = txn.exec_params(
                "UPDATE admin_requests "
                "SET status = $1, response_timestamp = NOW() "
                "WHERE topic_id = $2 AND requester_client_id = $3 AND status = 'pending' "
                "RETURNING id",
                status, topic_id, requester_id);

            if (update_result.empty())
            {
                ui::print_message("DbManager",
                                  "No pending request found for this topic/requester",
                                  ui::MessageType::WARNING);
                return false;
            }

            // Si fue aprobada, añadir a tabla de administradores
            if (approved)
            {
                // Eliminar cualquier administrador existente para este tópico
                txn.exec_params(
                    "DELETE FROM topic_admins WHERE topic_id = $1",
                    topic_id);

                // Insertar nuevo administrador
                txn.exec_params(
                    "INSERT INTO topic_admins (topic_id, admin_client_id) "
                    "VALUES ($1, $2)",
                    topic_id, requester_id);
            }

            txn.commit();

            ui::print_message("DbManager",
                              "Admin request for topic " + topic_name + " by " + requester_id +
                                  " has been " + status,
                              ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error responding to admin request: " + std::string(e.what()),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::revoke_admin_status(const std::string &topic_name, const std::string &owner_id,
                                        const std::string &admin_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar que el tópico existe y el dueño es correcto
            auto topic_result = txn.exec_params(
                "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                topic_name, owner_id);

            if (topic_result.empty())
            {
                ui::print_message("DbManager",
                                  "Topic not found or user is not owner: " + topic_name,
                                  ui::MessageType::WARNING);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Eliminar de tabla de administradores
            auto delete_result = txn.exec_params(
                "DELETE FROM topic_admins "
                "WHERE topic_id = $1 AND admin_client_id = $2 "
                "RETURNING topic_id",
                topic_id, admin_id);

            if (delete_result.empty())
            {
                ui::print_message("DbManager",
                                  "Admin not found for this topic",
                                  ui::MessageType::WARNING);
                return false;
            }

            txn.commit();

            ui::print_message("DbManager",
                              "Admin status revoked for " + admin_id + " on topic " + topic_name,
                              ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error revoking admin status: " + std::string(e.what()),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::is_topic_admin(const std::string &topic_name, const std::string &client_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar si el usuario es dueño del tópico
            auto owner_result = txn.exec_params(
                "SELECT 1 FROM topics WHERE name = $1 AND owner_client_id = $2",
                topic_name, client_id);

            if (!owner_result.empty())
            {
                return true; // El dueño siempre es administrador
            }

            // Verificar si es administrador adicional
            auto admin_result = txn.exec_params(
                "SELECT 1 FROM topic_admins ta "
                "JOIN topics t ON ta.topic_id = t.id "
                "WHERE t.name = $1 AND ta.admin_client_id = $2",
                topic_name, client_id);

            return !admin_result.empty();
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error checking admin status: " + std::string(e.what()),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::set_sensor_status(const std::string &topic_name, const std::string &sensor_name,
                                      const std::string &client_id, bool active)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar que el usuario es dueño o administrador
            if (!is_topic_admin(topic_name, client_id))
            {
                ui::print_message("DbManager",
                                  "User is not owner or admin of topic: " + topic_name,
                                  ui::MessageType::WARNING);
                return false;
            }

            // Obtener ID del tópico
            auto topic_result = txn.exec_params(
                "SELECT id FROM topics WHERE name = $1",
                topic_name);

            if (topic_result.empty())
            {
                ui::print_message("DbManager",
                                  "Topic not found: " + topic_name,
                                  ui::MessageType::WARNING);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Insertar o actualizar configuración del sensor
            txn.exec_params(
                "INSERT INTO admin_sensor_config (topic_id, sensor_name, active, set_by, updated_at) "
                "VALUES ($1, $2, $3, $4, NOW()) "
                "ON CONFLICT (topic_id, sensor_name) DO UPDATE "
                "SET active = $3, set_by = $4, updated_at = NOW()",
                topic_id, sensor_name, active, client_id);

            txn.commit();

            ui::print_message("DbManager",
                              "Sensor " + sensor_name + " " + (active ? "activated" : "deactivated") + " for topic " + topic_name,
                              ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager",
                              "Error setting sensor status: " + std::string(e.what()),
                              ui::MessageType::ERROR);
            return false;
        }
    }

} // namespace tinymq