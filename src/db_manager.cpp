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
                // Update existing client
                std::stringstream query;
                query << "UPDATE clients SET last_connected = CURRENT_TIMESTAMP, "
                      << "last_ip = " << txn.quote(ip_address)
                      << ", last_port = " << port
                      << ", connection_count = connection_count + 1 "
                      << "WHERE client_id = " << txn.quote(client_id);

                txn.exec(query.str());
            }
            else
            {
                // Insert new client
                std::stringstream query;
                query << "INSERT INTO clients (client_id, last_ip, last_port) VALUES ("
                      << txn.quote(client_id) << ", "
                      << txn.quote(ip_address) << ", "
                      << port << ")";

                txn.exec(query.str());
            }

            // Log the connection event
            std::stringstream event_query;
            event_query << "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
                        << "VALUES (" << txn.quote(client_id) << ", 'CONNECT', "
                        << txn.quote(ip_address) << ", " << port << ")";

            txn.exec(event_query.str());

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
            auto result = txn.exec(
                "SELECT last_ip, last_port FROM clients WHERE client_id = $1",
                pqxx::params(client_id));

            if (result.empty())
            {
                ui::print_message("Database", "Client not found for disconnect: " + client_id,
                                  ui::MessageType::WARNING);
                return false;
            }

            std::string ip = result[0]["last_ip"].as<std::string>();
            int port = result[0]["last_port"].as<int>();

            // Log the disconnect event
            txn.exec(
                "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
                "VALUES ($1, 'DISCONNECT', $2, $3)",
                pqxx::params(client_id, ip, port));

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
                txn.exec(
                    "INSERT INTO topics (name, owner_client_id) VALUES ($1, $2)",
                    pqxx::params(pure_topic_name, owner_client_id));

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
            auto result = txn.exec(
                "SELECT id FROM topics WHERE name = $1 AND owner_client_id = $2",
                pqxx::params(pure_topic_name, owner_client_id));

            if (result.empty())
            {
                ui::print_message("Database",
                                  "No se puede actualizar el estado de publicación: tópico no encontrado o no es propietario",
                                  ui::MessageType::WARNING);
                return false;
            }

            // Actualizar el estado de publicación
            txn.exec(
                "UPDATE topics SET publish = $1 WHERE name = $2 AND owner_client_id = $3",
                pqxx::params(publish, pure_topic_name, owner_client_id));

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

            auto result = txn.exec(
                "SELECT id FROM topics WHERE name = $1",
                pqxx::params(pure_topic_name));

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
            auto topic_result = txn.exec(
                "SELECT name FROM topics WHERE id = $1",
                pqxx::params(topic_id));

            if (topic_result.empty())
            {
                ui::print_message("Database", "Cannot add subscription: topic ID does not exist: " + std::to_string(topic_id), ui::MessageType::WARNING);
                return false;
            }

            // Check if subscription already exists
            auto sub_result = txn.exec(
                "SELECT id, active FROM subscriptions WHERE client_id = $1 AND topic_id = $2",
                pqxx::params(client_id, topic_id));

            if (!sub_result.empty())
            {
                // Subscription exists - check if it's active
                bool active = sub_result[0]["active"].as<bool>();
                if (!active)
                {
                    // Reactivate it
                    txn.exec(
                        "UPDATE subscriptions SET active = TRUE, subscribed_at = CURRENT_TIMESTAMP "
                        "WHERE client_id = $1 AND topic_id = $2",
                        pqxx::params(client_id, topic_id));
                }
            }
            else
            {
                // Create new subscription
                txn.exec(
                    "INSERT INTO subscriptions (client_id, topic_id) VALUES ($1, $2)",
                    pqxx::params(client_id, topic_id));
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

            auto result = txn.exec(
                "UPDATE subscriptions SET active = FALSE "
                "WHERE client_id = $1 AND topic_id = $2",
                pqxx::params(client_id, topic_id));

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

            txn.exec(
                "INSERT INTO message_logs (publisher_client_id, topic_id, payload_size, payload_preview) "
                "VALUES ($1, $2, $3, $4)",
                pqxx::params(publisher_client_id, topic_id, static_cast<int>(payload_size), payload_preview));

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
        auto result = txn.exec(
            "SELECT 1 FROM clients WHERE client_id = $1",
            pqxx::params(client_id));
        return !result.empty();
    }

    bool DbManager::topic_exists(pqxx::work &txn, const std::string &topic_name)
    {
        auto result = txn.exec(
            "SELECT 1 FROM topics WHERE name = $1",
            pqxx::params(topic_name));
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

            for (auto row : result)
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

} // namespace tinymq