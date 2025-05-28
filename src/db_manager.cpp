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

    // Versión pública de client_exists
    bool DbManager::client_exists(const std::string &client_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            return client_exists(txn, client_id);
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking client existence: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    std::vector<std::map<std::string, std::string>> DbManager::get_admin_topics(const std::string &admin_client_id)
    {
        std::vector<std::map<std::string, std::string>> topics;
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = exec_params(txn,
                                      "SELECT t.name, t.owner_client_id, t.publish, ta.granted_at "
                                      "FROM topics t "
                                      "JOIN topic_admins ta ON t.id = ta.topic_id "
                                      "WHERE ta.admin_client_id = $1 "
                                      "ORDER BY t.name",
                                      admin_client_id);

            for (const auto &row : result)
            {
                std::map<std::string, std::string> topic;
                topic["name"] = row["name"].as<std::string>();
                topic["owner_client_id"] = row["owner_client_id"].as<std::string>();
                topic["publish"] = row["publish"].as<bool>() ? "true" : "false";
                topic["granted_at"] = row["granted_at"].as<std::string>();
                topics.push_back(topic);
            }

            ui::print_message("Database", "Retrieved " + std::to_string(topics.size()) + " admin topics for " + admin_client_id, ui::MessageType::INFO);
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting admin topics: ") + e.what(), ui::MessageType::ERROR);
        }
        return topics;
    }

    bool DbManager::resign_admin_status(const std::string &topic_name, const std::string &admin_client_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Obtener topic_id
            auto topic_result = exec_params(txn,
                                            "SELECT id FROM topics WHERE name = $1",
                                            topic_name);

            if (topic_result.empty())
            {
                ui::print_message("DbManager", "Topic not found: " + topic_name, ui::MessageType::WARNING);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Verificar que el cliente es administrador
            auto admin_result = exec_params(txn,
                                            "SELECT 1 FROM topic_admins WHERE topic_id = $1 AND admin_client_id = $2",
                                            topic_id, admin_client_id);

            if (admin_result.empty())
            {
                ui::print_message("DbManager", "User is not admin of this topic", ui::MessageType::WARNING);
                return false;
            }

            // Eliminar de topic_admins
            exec_params(txn,
                        "DELETE FROM topic_admins WHERE topic_id = $1 AND admin_client_id = $2",
                        topic_id, admin_client_id);

            exec_params(txn,
                        "UPDATE admin_requests SET status = 'revoked', response_timestamp = NOW() "
                        "WHERE topic_id = $1 AND requester_client_id = $2 AND status = 'approved'",
                        topic_id, admin_client_id);

            txn.commit();

            ui::print_message("DbManager", admin_client_id + " resigned from admin of topic " + topic_name, ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager", "Error resigning admin status: " + std::string(e.what()), ui::MessageType::ERROR);
            return false;
        }
    }

    std::vector<std::map<std::string, std::string>> DbManager::get_topic_sensors_config(const std::string &topic_name, const std::string &admin_client_id)
    {
        ui::print_message("DbManager", "INICIO get_topic_sensors_config para tópico: " + topic_name, ui::MessageType::DEBUG);

        std::vector<std::map<std::string, std::string>> sensors;
        try
        {
            ui::print_message("DbManager", "Intentando adquirir mutex...", ui::MessageType::DEBUG);
            std::lock_guard<std::mutex> lock(db_mutex_);
            ui::print_message("DbManager", "Mutex adquirido", ui::MessageType::DEBUG);

            ui::print_message("DbManager", "Abriendo conexión a la base de datos...", ui::MessageType::DEBUG);
            pqxx::connection conn(connection_string_);
            ui::print_message("DbManager", "Conexión a BD abierta", ui::MessageType::DEBUG);

            ui::print_message("DbManager", "Iniciando transacción...", ui::MessageType::DEBUG);
            pqxx::work txn(conn);
            ui::print_message("DbManager", "Transacción iniciada", ui::MessageType::DEBUG);

            // Verificar que es administrador o dueño
            ui::print_message("DbManager", "Verificando si es admin...", ui::MessageType::DEBUG);
            if (!is_topic_admin(txn, topic_name, admin_client_id))
            {
                ui::print_message("DbManager", "User is not admin of topic: " + topic_name, ui::MessageType::WARNING);
                return sensors;
            }
            ui::print_message("DbManager", "Es admin, buscando topic_id...", ui::MessageType::DEBUG);

            auto topic_result = exec_params(txn,
                                            "SELECT id FROM topics WHERE name = $1",
                                            topic_name);

            ui::print_message("DbManager", "Consulta de topic_id ejecutada", ui::MessageType::DEBUG);

            if (topic_result.empty())
            {
                ui::print_message("DbManager", "Topic no encontrado: " + topic_name, ui::MessageType::WARNING);
                return sensors;
            }

            int topic_id = topic_result[0]["id"].as<int>();
            ui::print_message("DbManager", "topic_id obtenido: " + std::to_string(topic_id), ui::MessageType::DEBUG);

            // Obtener configuración de sensores
            ui::print_message("DbManager", "Consultando admin_sensor_config...", ui::MessageType::DEBUG);
            auto result = exec_params(txn,
                                      "SELECT sensor_name, active, activable, updated_at " // AGREGAR 'activable'
                                      "FROM admin_sensor_config "
                                      "WHERE topic_id = $1 "
                                      "ORDER BY sensor_name",
                                      topic_id);

            for (const auto &row : result)
            {
                std::string sensor_name = row["sensor_name"].as<std::string>();
                bool active_value = row["active"].as<bool>();
                bool activable_value = row["activable"].as<bool>();
                std::string updated_at = row["updated_at"].as<std::string>();

                ui::print_message(
                    "DbManager",
                    "Sensor: " + sensor_name +
                        ", active=" + (active_value ? "true" : "false") +
                        ", activable=" + (activable_value ? "true" : "false") +
                        ", updated_at=" + updated_at,
                    ui::MessageType::DEBUG);

                std::map<std::string, std::string> sensor;
                sensor["name"] = sensor_name;
                sensor["active"] = active_value ? "true" : "false";
                sensor["activable"] = activable_value ? "true" : "false";
                sensor["configured_at"] = updated_at;
                sensors.push_back(sensor);
            }
            ui::print_message("DbManager", "Sensores procesados: " + std::to_string(sensors.size()), ui::MessageType::DEBUG);
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting topic sensors config: ") + e.what(), ui::MessageType::ERROR);
        }
        ui::print_message("DbManager", "FIN get_topic_sensors_config para tópico: " + topic_name, ui::MessageType::DEBUG);
        return sensors;
    }

    // Versión pública de topic_exists
    bool DbManager::topic_exists(const std::string &topic_name)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            return topic_exists(txn, topic_name);
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking topic existence: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::is_client_subscribed(const std::string &client_id, const std::string &topic_name)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = exec_params(txn,
                                      "SELECT 1 FROM subscriptions s "
                                      "JOIN topics t ON s.topic_id = t.id "
                                      "WHERE s.client_id = $1 AND t.name = $2 AND s.active = TRUE",
                                      client_id, topic_name);

            return !result.empty();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking subscription: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::topic_has_admin(const std::string &topic_name)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = exec_params(txn,
                                      "SELECT 1 FROM topic_admins ta "
                                      "JOIN topics t ON ta.topic_id = t.id "
                                      "WHERE t.name = $1",
                                      topic_name);

            return !result.empty();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking topic admin: ") + e.what(),
                              ui::MessageType::ERROR);
            return false;
        }
    }

    bool DbManager::add_topic_admin(const std::string &topic_name, const std::string &admin_client_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Obtener topic_id
            auto topic_result = exec_params(txn,
                                            "SELECT id FROM topics WHERE name = $1",
                                            topic_name);

            if (topic_result.empty())
            {
                ui::print_message("Database", "Topic not found: " + topic_name, ui::MessageType::ERROR);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Verificar que no hay administrador ya
            auto existing = exec_params(txn,
                                        "SELECT 1 FROM topic_admins WHERE topic_id = $1",
                                        topic_id);

            if (!existing.empty())
            {
                ui::print_message("Database", "Topic already has an administrator", ui::MessageType::WARNING);
                return false;
            }

            // Agregar el administrador
            exec_params(txn,
                        "INSERT INTO topic_admins (topic_id, admin_client_id) VALUES ($1, $2)",
                        topic_id, admin_client_id);

            txn.commit();
            ui::print_message("Database", "Admin privileges granted to " + admin_client_id + " for topic " + topic_name, ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error adding topic admin: ") + e.what(),
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
                exec_params(txn,
                            "UPDATE clients SET last_connected = CURRENT_TIMESTAMP, "
                            "last_ip = $1, last_port = $2, connection_count = connection_count + 1, active = TRUE "
                            "WHERE client_id = $3",
                            ip_address, port, client_id);
            }
            else
            {
                exec_params(txn,
                            "INSERT INTO clients (client_id, last_ip, last_port, active) VALUES ($1, $2, $3, TRUE)",
                            client_id, ip_address, port);
            }

            exec_params(txn,
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

            auto result = exec_params(txn,
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

            exec_params(txn,
                        "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
                        "VALUES ($1, 'DISCONNECT', $2, $3)",
                        client_id, ip, port);

            exec_params(txn,
                        "UPDATE clients SET active = FALSE WHERE client_id = $1",
                        client_id);

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
                exec_params(txn,
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

            auto result = exec_params(txn,
                                      "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                                      pure_topic_name, owner_client_id);

            if (result.empty())
            {
                ui::print_message("Database",
                                  "No se puede actualizar el estado de publicación: tópico no encontrado o no es propietario",
                                  ui::MessageType::WARNING);
                return false;
            }

            exec_params(txn,
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

            auto result = exec_params(txn,
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

            auto topic_result = exec_params(txn,
                                            "SELECT name FROM topics WHERE id = $1",
                                            topic_id);

            if (topic_result.empty())
            {
                ui::print_message("Database", "Cannot add subscription: topic ID does not exist: " + std::to_string(topic_id), ui::MessageType::WARNING);
                return false;
            }

            auto sub_result = exec_params(txn,
                                          "SELECT id, active FROM subscriptions WHERE client_id = $1 AND topic_id = $2",
                                          client_id, topic_id);

            if (!sub_result.empty())
            {
                // Subscription exists - check if it's active
                bool active = sub_result[0]["active"].as<bool>();
                if (!active)
                {
                    exec_params(txn,
                                "UPDATE subscriptions SET active = TRUE, subscribed_at = CURRENT_TIMESTAMP "
                                "WHERE client_id = $1 AND topic_id = $2",
                                client_id, topic_id);
                }
            }
            else
            {
                exec_params(txn,
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

            exec_params(txn,
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

            exec_params(txn,
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
        auto result = exec_params(txn,
                                  "SELECT 1 FROM clients WHERE client_id = $1",
                                  client_id);
        return !result.empty();
    }

    bool DbManager::topic_exists(pqxx::work &txn, const std::string &topic_name)
    {
        auto result = exec_params(txn,
                                  "SELECT 1 FROM topics WHERE name = $1",
                                  topic_name);
        return !result.empty();
    }

    bool DbManager::setup_schema()
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            try
            {
                // Attempt a benign query on a key table. Fully qualify the table name.
                txn.exec("SELECT 1 FROM public.clients LIMIT 1");
                // If the above query succeeds, the table (and thus the schema) exists.
                ui::print_message("Database", "Database schema (clients table found) already exists.", ui::MessageType::INFO);
                txn.commit(); // Commit (or let it auto-commit) as the check passed and no schema changes are needed.
                return true;
            }
            catch (const pqxx::sql_error &e)
            {
                // Check if the error is "undefined_table" (SQLSTATE 42P01)
                std::string sqlstate = e.sqlstate();
                if (sqlstate == "42P01") // 42P01 indicates undefined_table
                {
                    ui::print_message("Database", "Database schema not found (clients table missing, SQLSTATE: 42P01), attempting creation...", ui::MessageType::INFO);
                    // Table does not exist, so we proceed to create the schema.
                    // The transaction 'txn' is still active and will be used for schema creation.
                }
                else
                {
                    // A different SQL error occurred during the existence check. This is unexpected.
                    ui::print_message("Database", std::string("Unexpected SQL error during schema check: ") + e.what() + " (SQLSTATE: " + sqlstate + ")", ui::MessageType::ERROR);
                    txn.abort(); // Abort the transaction.
                    return false;
                }
            }
            // If we've reached this point, it means the 'clients' table was not found (42P01),
            // and we should proceed with creating the schema.

            std::ifstream schema_file("db/schema.sql");
            if (!schema_file.is_open())
            {
                ui::print_message("Database", "Failed to open schema file (db/schema.sql)", ui::MessageType::ERROR);
                txn.abort(); // Abort the transaction.
                return false;
            }

            std::stringstream schema_stream;
            schema_stream << schema_file.rdbuf();
            std::string schema_sql = schema_stream.str();

            if (schema_sql.empty())
            {
                ui::print_message("Database", "Schema file (db/schema.sql) is empty.", ui::MessageType::ERROR);
                txn.abort(); // Abort the transaction.
                return false;
            }

            // Execute the entire schema.sql script.
            txn.exec(schema_sql);
            txn.commit(); // Commit the transaction after successful schema execution.

            ui::print_message("Database", "Database schema successfully created.", ui::MessageType::SUCCESS);
            return true;
        }
        catch (const pqxx::sql_error &e) // Catch errors specifically from schema creation itself or commit.
        {
            ui::print_message("Database", std::string("SQL error during schema setup/execution: ") + e.what() + " (Query context: " + (e.query().empty() ? "N/A" : e.query()) + ") (SQLSTATE: " + e.sqlstate() + ")",
                              ui::MessageType::ERROR);
            // Note: If txn was used, it would have been aborted by pqxx::work destructor on exception if not committed/aborted explicitly.
            return false;
        }
        catch (const std::exception &e) // Catch other generic errors.
        {
            ui::print_message("Database", std::string("Generic error during schema setup: ") + e.what(),
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

            auto result = exec_params(txn,
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

            std::string query =
                "SELECT ar.id, ar.requester_client_id, t.name as topic, ar.status, "
                "EXTRACT(EPOCH FROM ar.request_timestamp) as request_time "
                "FROM admin_requests ar "
                "JOIN topics t ON ar.topic_id = t.id "
                "WHERE t.owner_client_id = $1";

            if (only_pending)
            {
                query += " AND ar.status = 'pending'";
            }

            query += " ORDER BY ar.request_timestamp DESC";

            auto result = exec_params(txn, query, owner_id);

            for (const auto &row : result)
            {
                std::map<std::string, std::string> request;
                request["id"] = row["id"].as<std::string>();
                request["requester_client_id"] = row["requester_client_id"].as<std::string>();
                request["topic"] = row["topic"].as<std::string>();
                request["status"] = row["status"].as<std::string>();
                request["request_time"] = row["request_time"].as<std::string>();
                requests.push_back(request);
            }
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting admin requests: ") + e.what(),
                              ui::MessageType::ERROR);
        }
        return requests;
    }

    std::string DbManager::check_admin_request_status(const std::string &topic_name, const std::string &requester_id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto topic_result = exec_params(txn,
                                            "SELECT id FROM topics WHERE name = $1",
                                            topic_name);

            if (topic_result.empty())
            {
                return "TOPIC_NOT_FOUND";
            }

            int topic_id = topic_result[0]["id"].as<int>();

            auto request_result = exec_params(txn,
                                              "SELECT status FROM admin_requests WHERE topic_id = $1 AND requester_client_id = $2",
                                              topic_id, requester_id);

            if (request_result.empty())
            {
                return "NO_REQUEST";
            }

            return request_result[0]["status"].as<std::string>();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking admin request status: ") + e.what(),
                              ui::MessageType::ERROR);
            return "ERROR";
        }
    }

    bool DbManager::request_admin_status(const std::string &topic_name, const std::string &requester_id)
    {
        try
        {
            // Crear una conexión local como en los otros métodos
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verificar que el topic existe
            auto topic_result = exec_params(txn,
                                            "SELECT id, owner_client_id FROM topics WHERE name = $1",
                                            topic_name);

            if (topic_result.empty())
            {
                ui::print_message("Database", "Topic not found: " + topic_name, ui::MessageType::ERROR);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // VERIFICAR SI EXISTE UNA SOLICITUD ANTERIOR
            auto existing = exec_params(txn,
                                        "SELECT id, status FROM admin_requests WHERE topic_id = $1 AND requester_client_id = $2",
                                        topic_id, requester_id);

            if (!existing.empty())
            {
                std::string current_status = existing[0]["status"].as<std::string>();

                // Solo bloquear si está pendiente
                if (current_status == "pending")
                {
                    ui::print_message("Database", "Admin request already pending", ui::MessageType::WARNING);
                    return false;
                }

                // Si fue rechazada, revocada, o aprobada anteriormente, permitir nueva solicitud
                // actualizando el registro existente
                ui::print_message("Database", "Updating previous admin request (status was: " + current_status + ")", ui::MessageType::INFO);
                exec_params(txn,
                            "UPDATE admin_requests SET status = 'pending', request_timestamp = NOW(), response_timestamp = NULL "
                            "WHERE topic_id = $1 AND requester_client_id = $2",
                            topic_id, requester_id);

                ui::print_message("Database", "Admin request updated to pending", ui::MessageType::SUCCESS);
            }
            else
            {
                // Insertar nueva solicitud
                exec_params(txn,
                            "INSERT INTO admin_requests (topic_id, requester_client_id, status) VALUES ($1, $2, 'pending')",
                            topic_id, requester_id);

                ui::print_message("Database", "New admin request created", ui::MessageType::SUCCESS);
            }

            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error requesting admin status: ") + e.what(),
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

            auto topic_result = exec_params(txn,
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
            auto update_result = exec_params(txn,
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
                exec_params(txn,
                            "DELETE FROM topic_admins WHERE topic_id = $1",
                            topic_id);

                exec_params(txn,
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

            // Verificar que el usuario es dueño del tópico
            auto topic_result = exec_params(txn,
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

            // Eliminar al administrador de la tabla topic_admins
            auto delete_result = exec_params(txn,
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

            // AGREGAR: Actualizar el estado de la solicitud original a 'revoked'
            exec_params(txn,
                        "UPDATE admin_requests SET status = 'revoked', response_timestamp = NOW() "
                        "WHERE topic_id = $1 AND requester_client_id = $2 AND status = 'approved'",
                        topic_id, admin_id);

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

    // En DbManager.cpp
    std::vector<std::map<std::string, std::string>> DbManager::get_client_admin_requests(const std::string &requester_id)
    {
        std::vector<std::map<std::string, std::string>> requests;
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // CORREGIR: Usar TO_CHAR para formatear la fecha correctamente
            auto result = exec_params(txn,
                                      "SELECT ar.id, t.name as topic_name, t.owner_client_id, ar.status, "
                                      "TO_CHAR(ar.request_timestamp, 'YYYY-MM-DD HH24:MI:SS') as request_time "
                                      "FROM admin_requests ar "
                                      "JOIN topics t ON ar.topic_id = t.id "
                                      "WHERE ar.requester_client_id = $1 "
                                      "ORDER BY ar.request_timestamp DESC",
                                      requester_id);

            for (const auto &row : result)
            {
                std::map<std::string, std::string> request_info;
                request_info["id"] = row["id"].as<std::string>();
                request_info["topic_name"] = row["topic_name"].as<std::string>();
                request_info["owner_client_id"] = row["owner_client_id"].as<std::string>();
                request_info["status"] = row["status"].as<std::string>();
                request_info["request_time"] = row["request_time"].as<std::string>();

                requests.push_back(request_info);
            }

            txn.commit();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting client admin requests: ") + e.what(),
                              ui::MessageType::ERROR);
        }
        return requests;
    }
    bool DbManager::is_topic_admin(pqxx::work &txn, const std::string &topic_name, const std::string &client_id)
    {
        // NO lock aquí
        auto result = exec_params(txn,
                                  "SELECT 1 FROM topic_admins ta "
                                  "JOIN topics t ON ta.topic_id = t.id "
                                  "WHERE t.name = $1 AND ta.admin_client_id = $2",
                                  topic_name, client_id);
        return !result.empty();
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
            if (!is_topic_admin(txn, topic_name, client_id))
            {
                ui::print_message("DbManager",
                                  "User is not owner or admin of topic: " + topic_name,
                                  ui::MessageType::WARNING);
                return false;
            }

            auto topic_result = exec_params(txn,
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

            exec_params(txn,
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

    std::vector<std::map<std::string, std::string>> DbManager::get_my_topics(const std::string &owner_id)
    {
        std::vector<std::map<std::string, std::string>> topics;
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = exec_params(txn,
                                      "SELECT t.name, t.publish, t.created_at, ta.admin_client_id "
                                      "FROM topics t "
                                      "LEFT JOIN topic_admins ta ON t.id = ta.topic_id "
                                      "WHERE t.owner_client_id = $1 "
                                      "ORDER BY t.created_at DESC",
                                      owner_id);

            for (const auto &row : result)
            {
                std::map<std::string, std::string> topic;
                topic["name"] = row["name"].as<std::string>();
                topic["publish_active"] = row["publish"].as<bool>() ? "true" : "false";
                topic["admin_client_id"] = row["admin_client_id"].is_null() ? "" : row["admin_client_id"].as<std::string>();
                topic["created_at"] = row["created_at"].as<std::string>();
                topics.push_back(topic);
            }

            txn.commit();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error getting my topics: ") + e.what(),
                              ui::MessageType::ERROR);
        }
        return topics;
    }

    bool DbManager::set_sensor_activable(const std::string &topic_name, const std::string &sensor_name, const std::string &client_id, bool activable)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Verifica que el client_id sea dueño del tópico
            auto topic_result = exec_params(txn,
                                            "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                                            topic_name, client_id);

            if (topic_result.empty())
            {
                ui::print_message("DbManager", "No eres dueño del tópico: " + topic_name, ui::MessageType::WARNING);
                return false;
            }

            int topic_id = topic_result[0]["id"].as<int>();

            // Actualiza o inserta el estado activable del sensor
            exec_params(txn,
                        "INSERT INTO admin_sensor_config (topic_id, sensor_name, activable, set_by, updated_at) "
                        "VALUES ($1, $2, $3, $4, NOW()) "
                        "ON CONFLICT (topic_id, sensor_name) DO UPDATE "
                        "SET activable = $3, set_by = $4, updated_at = NOW()",
                        topic_id, sensor_name, activable, client_id);

            txn.commit();
            ui::print_message("DbManager", "Sensor " + sensor_name + " en tópico " + topic_name + " marcado como activable=" + std::string(activable ? "true" : "false"), ui::MessageType::SUCCESS);
            return true;
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager", "Error en set_sensor_activable: " + std::string(e.what()), ui::MessageType::ERROR);
            return false;
        }
    }

    std::string DbManager::get_topic_owner(const std::string &topic_name)
    {
        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            auto result = exec_params(txn,
                                      "SELECT owner_client_id FROM topics WHERE name = $1",
                                      topic_name);

            if (result.empty())
            {
                ui::print_message("DbManager", "No se encontró propietario para el tópico: " + topic_name, ui::MessageType::WARNING);
                return "";
            }

            return result[0]["owner_client_id"].as<std::string>();
        }
        catch (const std::exception &e)
        {
            ui::print_message("DbManager", std::string("Error obteniendo propietario del tópico: ") + e.what(), ui::MessageType::ERROR);
            return "";
        }
    }

    std::vector<std::string> DbManager::get_inactive_clients(const std::vector<std::string> &client_ids)
    {
        std::vector<std::string> inactive_clients;

        if (client_ids.empty())
        {
            return inactive_clients;
        }

        try
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);

            // Build a query to check all client IDs at once
            std::string placeholders;
            for (size_t i = 0; i < client_ids.size(); ++i)
            {
                if (i > 0)
                    placeholders += ",";
                placeholders += "$" + std::to_string(i + 1);
            }

            std::string query = "SELECT client_id FROM clients WHERE client_id IN (" + placeholders + ") AND active = FALSE";

            // Execute query with all client IDs as parameters
            pqxx::result result;
            if (client_ids.size() == 1)
            {
                result = exec_params(txn, query, client_ids[0]);
            }
            else if (client_ids.size() == 2)
            {
                result = exec_params(txn, query, client_ids[0], client_ids[1]);
            }
            else if (client_ids.size() == 3)
            {
                result = exec_params(txn, query, client_ids[0], client_ids[1], client_ids[2]);
            }
            else if (client_ids.size() == 4)
            {
                result = exec_params(txn, query, client_ids[0], client_ids[1], client_ids[2], client_ids[3]);
            }
            else if (client_ids.size() == 5)
            {
                result = exec_params(txn, query, client_ids[0], client_ids[1], client_ids[2], client_ids[3], client_ids[4]);
            }
            else
            {
                // For more than 5 clients, use a simpler approach
                for (const auto &client_id : client_ids)
                {
                    auto single_result = exec_params(txn, "SELECT client_id FROM clients WHERE client_id = $1 AND active = FALSE", client_id);
                    if (!single_result.empty())
                    {
                        inactive_clients.push_back(client_id);
                    }
                }
                txn.commit();
                return inactive_clients;
            }

            for (const auto &row : result)
            {
                inactive_clients.push_back(row["client_id"].as<std::string>());
            }

            txn.commit();
        }
        catch (const std::exception &e)
        {
            ui::print_message("Database", std::string("Error checking inactive clients: ") + e.what(),
                              ui::MessageType::ERROR);
        }

        return inactive_clients;
    }
} // namespace tinymq
