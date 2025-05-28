#include "broker.h"
#include "session.h"
#include "terminal_ui.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <chrono>

namespace tinymq
{

    Broker::Broker(uint16_t port, size_t thread_pool_size, const std::string &db_connection_str)
        : io_context_(),
          acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
          thread_pool_size_(thread_pool_size),
          running_(false),
          activity_timer_(std::make_unique<boost::asio::steady_timer>(io_context_))
    {

        if (!db_connection_str.empty())
        {
            db_manager_ = std::make_unique<DbManager>(db_connection_str);
            ui::print_message("Broker", "Database support enabled", ui::MessageType::INFO);
        }
        else
        {
            ui::print_message("Broker", "Running without database support", ui::MessageType::WARNING);
        }
    }

    Broker::~Broker()
    {
        stop();
    }

    void Broker::start()
    {
        if (running_)
        {
            return;
        }

        running_ = true;

        // Initialize database if configured
        if (db_manager_)
        {
            if (!db_manager_->initialize())
            {
                ui::print_message("Broker", "Failed to initialize database, continuing without DB support",
                                  ui::MessageType::ERROR);
                db_manager_.reset();
            }
            else
            {
                // Start client activity monitoring if database is available
                start_client_activity_check();
            }
        }

        accept_connections();

        threads_.reserve(thread_pool_size_);
        for (size_t i = 0; i < thread_pool_size_; ++i)
        {
            threads_.emplace_back([this]()
                                  {
            try {
                io_context_.run();
            } catch (const std::exception& e) {
                ui::print_message("Thread", "Exception: " + std::string(e.what()), ui::MessageType::ERROR);
            } });
        }

        ui::print_message("Broker", "Started on port " + std::to_string(acceptor_.local_endpoint().port()) + " with " + std::to_string(thread_pool_size_) + " threads",
                          ui::MessageType::SUCCESS);
    }

    void Broker::send_my_admin_topics(std::shared_ptr<Session> session)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available for admin topics request", ui::MessageType::WARNING);
            return;
        }

        const std::string &client_id = session->client_id();
        auto topics = db_manager_->get_admin_topics(client_id);

        // Serializar como JSON
        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &topic : topics)
        {
            nlohmann::json json_topic;
            json_topic["name"] = topic.at("name");
            json_topic["owner_client_id"] = topic.at("owner_client_id");
            json_topic["publish"] = topic.at("publish");
            json_topic["granted_at"] = topic.at("granted_at");
            json_array.push_back(json_topic);
        }

        std::string json_str = json_array.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet response_packet(PacketType::MY_ADMIN_TOPICS_RESP, 0, payload);
        session->send_packet(response_packet);

        ui::print_message("Broker", "Sent " + std::to_string(topics.size()) + " admin topics to " + client_id, ui::MessageType::INFO);
    }

    void Broker::handle_admin_resignation(std::shared_ptr<Session> session, const std::string &topic_name)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available for admin resignation", ui::MessageType::WARNING);
            return;
        }

        const std::string &admin_id = session->client_id();

        if (db_manager_->resign_admin_status(topic_name, admin_id))
        {
            // Éxito - enviar confirmación
            nlohmann::json response = {
                {"success", true},
                {"topic_name", topic_name},
                {"message", "Has renunciado exitosamente a la administración de este tópico"}};

            std::string response_str = response.dump();
            std::vector<uint8_t> payload(response_str.begin(), response_str.end());
            Packet ack_packet(PacketType::ADMIN_RESIGN_ACK, 0, payload);
            session->send_packet(ack_packet);

            ui::print_message("Broker", admin_id + " resigned from admin of topic " + topic_name, ui::MessageType::SUCCESS);

            // Notificar al dueño del tópico
            std::string owner_id = extract_client_id_from_topic(topic_name);
            if (!owner_id.empty())
            {
                notify_admin_resignation_to_owner(owner_id, topic_name, admin_id);
            }
        }
        else
        {
            // Error - enviar mensaje de error
            nlohmann::json response = {
                {"success", false},
                {"topic_name", topic_name},
                {"message", "No se pudo procesar la renuncia. Verifica que seas administrador de este tópico."}};

            std::string response_str = response.dump();
            std::vector<uint8_t> payload(response_str.begin(), response_str.end());
            Packet ack_packet(PacketType::ADMIN_RESIGN_ACK, 1, payload); // flag 1 = error
            session->send_packet(ack_packet);
        }
    }

    void Broker::send_topic_sensors_config(std::shared_ptr<Session> session, const std::string &topic_name)
    {
        if (!has_database())
        {
            // Responde con lista vacía
            nlohmann::json response = {{"topic_name", topic_name}, {"sensors", nlohmann::json::array()}};
            std::string json_str = response.dump();
            std::vector<uint8_t> payload(json_str.begin(), json_str.end());
            Packet response_packet(PacketType::TOPIC_SENSORS_RESP, 0, payload);
            session->send_packet(response_packet);
            return;
        }

        const std::string &client_id = session->client_id();
        try
        {
            auto sensors = db_manager_->get_topic_sensors_config(topic_name, client_id);

            nlohmann::json response = {
                {"topic_name", topic_name},
                {"sensors", nlohmann::json::array()}};
            for (const auto &sensor : sensors)
            {
                response["sensors"].push_back(sensor);
            }
            std::string json_str = response.dump();
            std::vector<uint8_t> payload(json_str.begin(), json_str.end());
            Packet response_packet(PacketType::TOPIC_SENSORS_RESP, 0, payload);
            session->send_packet(response_packet);

            ui::print_message("Broker", "Sent sensors config for topic " + topic_name + " to " + client_id, ui::MessageType::INFO);
        }
        catch (const std::exception &e)
        {
            ui::print_message("Broker", std::string("Error in send_topic_sensors_config: ") + e.what(), ui::MessageType::ERROR);
            // Enviar respuesta vacía para que el cliente no se quede esperando
            nlohmann::json response = {
                {"topic_name", topic_name},
                {"sensors", nlohmann::json::array()}};
            std::string json_str = response.dump();
            std::vector<uint8_t> payload(json_str.begin(), json_str.end());
            Packet response_packet(PacketType::TOPIC_SENSORS_RESP, 0, payload);
            session->send_packet(response_packet);
        }
    }

    // Método auxiliar para notificar al dueño sobre renuncia
    void Broker::notify_admin_resignation_to_owner(const std::string &owner_id, const std::string &topic_name, const std::string &resigned_admin_id)
    {
        std::shared_ptr<Session> owner_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(owner_id);
            if (it != sessions_.end())
            {
                owner_session = it->second;
            }
        }

        if (owner_session)
        {
            nlohmann::json notification = {
                {"__admin_resignation", true},
                {"topic_name", topic_name},
                {"resigned_admin", resigned_admin_id},
                {"timestamp", std::time(nullptr)}};

            std::string notification_str = notification.dump();
            std::vector<uint8_t> payload(notification_str.begin(), notification_str.end());
            Packet notification_packet(PacketType::ADMIN_NOTIFY, 0, payload);
            owner_session->send_packet(notification_packet);

            ui::print_message("Broker", "Notified owner " + owner_id + " about admin resignation", ui::MessageType::INFO);
        }
    }

    void Broker::handle_admin_request(std::shared_ptr<Session> requester_session, const std::string &topic_name)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available - cannot process admin request", ui::MessageType::WARNING);
            return;
        }

        const std::string &requester_id = requester_session->client_id();
        std::string owner_id = extract_client_id_from_topic(topic_name);

        if (owner_id.empty())
        {
            ui::print_message("Broker", "Invalid topic format for admin request: " + topic_name, ui::MessageType::ERROR);
            return;
        }

        if (owner_id == requester_id)
        {
            ui::print_message("Broker", "Client cannot request admin for their own topic", ui::MessageType::WARNING);
            notify_admin_request_error(requester_id, topic_name, "SELF_REQUEST",
                                       "No puedes solicitar administración de tu propio tópico");
            return;
        }

        // ========================================
        // VERIFICAR ESTADO ACTUAL DE LA SOLICITUD
        // ========================================
        std::string current_status = db_manager_->check_admin_request_status(topic_name, requester_id);

        if (current_status == "pending")
        {
            ui::print_message("Broker", "ℹ️ Admin request already pending", ui::MessageType::INFO);
            notify_admin_request_error(requester_id, topic_name, "ALREADY_PENDING",
                                       "Ya tienes una solicitud pendiente para este tópico. Espera la respuesta del propietario.");
            ui::print_message("Broker", "=== END ADMIN REQUEST ===", ui::MessageType::INFO);
            return;
        }

        // Verificaciones adicionales
        if (!db_manager_->client_exists(requester_id))
        {
            ui::print_message("Broker", "❌ Requester does not exist in database", ui::MessageType::ERROR);
            notify_admin_request_error(requester_id, topic_name, "REQUESTER_NOT_FOUND",
                                       "El solicitante no existe en el sistema");
            return;
        }

        if (!db_manager_->client_exists(owner_id))
        {
            ui::print_message("Broker", "❌ Owner does not exist in database", ui::MessageType::ERROR);
            notify_admin_request_error(requester_id, topic_name, "OWNER_NOT_FOUND",
                                       "El propietario del tópico no existe");
            return;
        }

        if (!db_manager_->topic_exists(topic_name))
        {
            ui::print_message("Broker", "❌ Topic does not exist", ui::MessageType::ERROR);
            notify_admin_request_error(requester_id, topic_name, "TOPIC_NOT_FOUND",
                                       "El tópico no existe");
            return;
        }

        if (!db_manager_->is_client_subscribed(requester_id, topic_name))
        {
            ui::print_message("Broker", "❌ Requester not subscribed to topic", ui::MessageType::ERROR);
            notify_admin_request_error(requester_id, topic_name, "NOT_SUBSCRIBED",
                                       "Debes estar suscrito al tópico para solicitar administración");
            return;
        }

        if (db_manager_->topic_has_admin(topic_name))
        {
            ui::print_message("Broker", "❌ Topic already has administrator", ui::MessageType::WARNING);
            notify_admin_request_error(requester_id, topic_name, "ALREADY_HAS_ADMIN",
                                       "El tópico ya tiene un administrador asignado");
            return;
        }

        // ========================================
        // PROCESAR LA SOLICITUD (SOLO SI PASÓ TODAS LAS VERIFICACIONES)
        // ========================================
        if (db_manager_->request_admin_status(topic_name, requester_id))
        {
            ui::print_message("Broker", "✅ Admin request registered in database", ui::MessageType::SUCCESS);

            // Notificar al dueño del tópico
            notify_admin_request(owner_id, topic_name, requester_id);
            ui::print_message("Broker", "✅ Owner notification sent", ui::MessageType::SUCCESS);

            // Enviar confirmación de éxito al solicitante
            nlohmann::json success_response = {
                {"__admin_request_success", true},
                {"topic_name", topic_name},
                {"message", "Solicitud de administración enviada correctamente"},
                {"timestamp", std::time(nullptr)}};

            std::string success_json = success_response.dump();
            std::vector<uint8_t> payload(success_json.begin(), success_json.end());
            Packet success_packet(PacketType::ADMIN_REQ_ACK, 0, payload); // flag 0 = éxito
            requester_session->send_packet(success_packet);

            // 🎉 MENSAJE DE ÉXITO COMPLETO
            ui::print_message("Broker", "🎉 ADMIN REQUEST COMPLETED SUCCESSFULLY! 🎉", ui::MessageType::SUCCESS);
            ui::print_message("Broker", "📋 SUMMARY:", ui::MessageType::SUCCESS);
            ui::print_message("Broker", "   • Requester: " + requester_id, ui::MessageType::SUCCESS);
            ui::print_message("Broker", "   • Topic: " + topic_name, ui::MessageType::SUCCESS);
            ui::print_message("Broker", "   • Owner: " + owner_id, ui::MessageType::SUCCESS);
            ui::print_message("Broker", "   • Status: PENDING OWNER APPROVAL", ui::MessageType::SUCCESS);
            ui::print_message("Broker", "==========================================", ui::MessageType::SUCCESS);
        }
        else
        {
            ui::print_message("Broker", "❌ Failed to register admin request", ui::MessageType::ERROR);
            notify_admin_request_error(requester_id, topic_name, "DATABASE_ERROR",
                                       "Error al registrar la solicitud en la base de datos");
        }

        ui::print_message("Broker", "=== END ADMIN REQUEST ===", ui::MessageType::INFO);
    }

    void Broker::handle_admin_response(std::shared_ptr<Session> owner_session, const std::string &topic_name,
                                       const std::string &requester_id, bool approved)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available - cannot process admin response", ui::MessageType::WARNING);
            return;
        }

        const std::string &owner_id = owner_session->client_id();

        // Procesar la respuesta en la base de datos
        if (db_manager_->respond_to_admin_request(topic_name, owner_id, requester_id, approved))
        {
            ui::print_message("Broker", "Admin response processed: " + topic_name + " -> " + requester_id + " = " + (approved ? "APPROVED" : "DENIED"), ui::MessageType::SUCCESS);

            // Notificar el resultado al solicitante
            notify_admin_result(requester_id, topic_name, approved, owner_id);
        }
        else
        {
            ui::print_message("Broker", "Failed to process admin response", ui::MessageType::ERROR);
        }
    }

    void Broker::send_admin_requests_list(std::shared_ptr<Session> session)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available - cannot get admin requests", ui::MessageType::WARNING);

            // Enviar respuesta vacía
            std::vector<uint8_t> empty_payload;
            Packet response_packet(PacketType::ADMIN_LIST_RESP, 0, empty_payload);
            session->send_packet(response_packet);
            return;
        }

        const std::string &client_id = session->client_id();

        // Obtener solicitudes pendientes para este cliente (como dueño)
        auto requests = db_manager_->get_admin_requests(client_id, true); // solo pendientes

        ui::print_message("Broker", "Sending " + std::to_string(requests.size()) + " admin requests to client: " + client_id, ui::MessageType::INFO);

        // Serializar como JSON
        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &request : requests)
        {
            nlohmann::json req_obj;
            req_obj["id"] = std::stoi(request.at("id"));
            req_obj["topic"] = request.at("topic");
            req_obj["requester_id"] = request.at("requester_client_id");
            req_obj["status"] = request.at("status");
            req_obj["request_time"] = std::stoll(request.at("request_time"));
            json_array.push_back(req_obj);
        }

        std::string json_str = json_array.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet response_packet(PacketType::ADMIN_LIST_RESP, 0, payload);
        session->send_packet(response_packet);
    }

    void Broker::notify_admin_result(const std::string &requester_id, const std::string &topic_name,
                                     bool approved, const std::string &owner_id)
    {
        // Buscar la sesión del solicitante
        std::shared_ptr<Session> requester_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(requester_id);
            if (it != sessions_.end())
            {
                requester_session = it->second;
            }
        }

        if (requester_session)
        {
            try
            {
                // Crear mensaje de resultado
                nlohmann::json result = {
                    {"__admin_result", true},
                    {"topic_name", topic_name},
                    {"approved", approved},
                    {"owner_id", owner_id},
                    {"timestamp", std::time(nullptr)}};

                std::string result_json = result.dump();
                std::vector<uint8_t> payload(result_json.begin(), result_json.end());
                Packet result_packet(PacketType::ADMIN_RESULT, 0, payload);
                requester_session->send_packet(result_packet);

                ui::print_message("Broker", "Admin result sent to " + requester_id + ": " + (approved ? "APPROVED" : "DENIED"), ui::MessageType::INFO);
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "Error sending admin result: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }
        else
        {
            ui::print_message("Broker", "Requester " + requester_id + " not connected for result notification",
                              ui::MessageType::INFO);
        }
    }

    void Broker::stop()
    {
        if (!running_)
        {
            return;
        }

        running_ = false;

        acceptor_.close();

        // Cancel the activity timer
        if (activity_timer_)
        {
            activity_timer_->cancel();
        }

        io_context_.stop();

        for (auto &thread : threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.clear();
        }

        {
            std::lock_guard<std::mutex> lock(topics_mutex_);
            topic_subscribers_.clear();
        }

        threads_.clear();

        ui::print_message("Broker", "Stopped", ui::MessageType::INFO);
    }

    void Broker::accept_connections()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    auto session = std::make_shared<Session>(std::move(socket), *this);
                    ui::print_message("Broker", "New connection from " + session->remote_endpoint(), ui::MessageType::INCOMING);
                    session->start();
                }
                else
                {
                    ui::print_message("Broker", "Accept error: " + ec.message(), ui::MessageType::ERROR);
                }

                if (running_)
                {
                    accept_connections();
                }
            });
    }

    void Broker::register_session(std::shared_ptr<Session> session)
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto &client_id = session->client_id();

        // Register in database if enabled
        if (db_manager_ && session->is_authenticated())
        {
            try
            {
                auto endpoint = session->remote_endpoint();
                size_t pos = endpoint.find_last_of(':');
                std::string ip = endpoint.substr(0, pos);
                int port = std::stoi(endpoint.substr(pos + 1));

                db_manager_->register_client(client_id, ip, port);
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "Error registering client in DB: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }

        auto it = sessions_.find(client_id);
        if (it != sessions_.end())
        {
            std::shared_ptr<Session> old_session = it->second;

            ui::print_message("Broker", "Client ID already in use, disconnecting old session: " + client_id,
                              ui::MessageType::WARNING);

            {
                std::lock_guard<std::mutex> topics_lock(topics_mutex_);
                for (auto &topic_entry : topic_subscribers_)
                {
                    auto &subscribers = topic_entry.second;
                    subscribers.erase(
                        std::remove_if(
                            subscribers.begin(),
                            subscribers.end(),
                            [&old_session](const std::shared_ptr<Session> &s)
                            {
                                return s == old_session;
                            }),
                        subscribers.end());
                }
            }

            it->second.reset();
        }

        sessions_[client_id] = session;
        ui::print_message("Broker", "Session registered: " + client_id, ui::MessageType::SUCCESS);
    }

    void Broker::remove_session(std::shared_ptr<Session> session)
    {
        const auto &client_id = session->client_id();

        if (client_id.empty())
        {
            return;
        }

        // Log client disconnect if database is enabled
        if (db_manager_)
        {
            db_manager_->log_client_disconnect(client_id);
        }

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.erase(client_id);
        }

        {
            std::lock_guard<std::mutex> lock(topics_mutex_);
            for (auto &topic_entry : topic_subscribers_)
            {
                auto &subscribers = topic_entry.second;
                subscribers.erase(
                    std::remove_if(
                        subscribers.begin(),
                        subscribers.end(),
                        [&session](const std::shared_ptr<Session> &s)
                        {
                            return s == session;
                        }),
                    subscribers.end());
            }
        }

        ui::print_message("Broker", "Session removed: " + client_id, ui::MessageType::INFO);
    }

    void Broker::subscribe(std::shared_ptr<Session> session, const std::string &topic)
    {
        const auto &client_id = session->client_id();

        // Comprobar primero si es un tópico específico de cliente (ej. Marco/tópico)
        std::string owner_client_id = extract_client_id_from_topic(topic);
        bool is_client_specific = !owner_client_id.empty();

        // Si no es específico de cliente, no registrar el tópico aquí
        // porque probablemente se publicará después con el prefijo del cliente
        if (is_client_specific && has_database())
        {
            int topic_id = db_manager_->get_topic_id(topic);
            if (topic_id == -1)
            {
                bool registrado = db_manager_->register_topic(topic, owner_client_id);
                if (registrado)
                {
                    ui::print_message("Broker", "Tópico registrado correctamente: " + topic, ui::MessageType::SUCCESS);
                    topic_id = db_manager_->get_topic_id(topic);
                }
                else
                {
                    ui::print_message("Broker", "No se pudo registrar el tópico: " + topic, ui::MessageType::ERROR);
                }
            }

            if (topic_id != -1)
            {
                db_manager_->add_subscription(client_id, topic_id);
            }
        }

        std::lock_guard<std::mutex> lock(topics_mutex_);

        auto &subscribers = topic_subscribers_[topic];
        if (std::find(subscribers.begin(), subscribers.end(), session) == subscribers.end())
        {
            subscribers.push_back(session);
            ui::print_message("Topic", "Client " + client_id + " subscribed to topic: " + topic, ui::MessageType::INFO);
        }
    }

    void Broker::unsubscribe(std::shared_ptr<Session> session, const std::string &topic)
    {
        const auto &client_id = session->client_id();

        // Update database
        if (db_manager_)
        {
            int topic_id = -1;

            // Check topic cache
            {
                std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                auto it = topic_id_cache_.find(topic);
                if (it != topic_id_cache_.end())
                {
                    topic_id = it->second;
                }
            }

            // If not in cache, try to get from DB
            if (topic_id == -1)
            {
                topic_id = db_manager_->get_topic_id(topic);

                // Cache it if found
                if (topic_id != -1)
                {
                    std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                    topic_id_cache_[topic] = topic_id;
                }
            }

            // Remove subscription in DB
            if (topic_id != -1)
            {
                db_manager_->remove_subscription(client_id, topic_id);
            }
        }

        std::lock_guard<std::mutex> lock(topics_mutex_);

        // Find the topic
        auto it = topic_subscribers_.find(topic);
        if (it != topic_subscribers_.end())
        {
            auto &subscribers = it->second;
            subscribers.erase(
                std::remove(subscribers.begin(), subscribers.end(), session),
                subscribers.end());

            ui::print_message("Topic", "Client " + client_id + " unsubscribed from topic: " + topic, ui::MessageType::INFO);

            if (subscribers.empty())
            {
                topic_subscribers_.erase(it);
            }
        }
    }

    void Broker::notify_admin_request_error(const std::string &requester_id,
                                            const std::string &topic_name,
                                            const std::string &error_code,
                                            const std::string &error_message)
    {
        // Buscar la sesión del solicitante
        std::shared_ptr<Session> requester_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(requester_id);
            if (it != sessions_.end())
            {
                requester_session = it->second;
            }
        }

        if (requester_session)
        {
            try
            {
                // Crear mensaje de error con código específico
                nlohmann::json error_response = {
                    {"__admin_request_error", true},
                    {"topic_name", topic_name},
                    {"error_code", error_code},
                    {"error_message", error_message},
                    {"timestamp", std::time(nullptr)}};

                std::string error_json = error_response.dump();
                std::vector<uint8_t> payload(error_json.begin(), error_json.end());

                // Usar flag 1 para indicar error
                Packet error_packet(PacketType::ADMIN_REQ_ACK, 1, payload);
                requester_session->send_packet(error_packet);

                // Log específico para diferentes tipos de error
                if (error_code == "ALREADY_PENDING")
                {
                    ui::print_message("Broker", "Admin request error sent to " + requester_id + ": Request already pending",
                                      ui::MessageType::INFO);
                }
                else if (error_code == "NOT_SUBSCRIBED")
                {
                    ui::print_message("Broker", "Admin request error sent to " + requester_id + ": Not subscribed to topic",
                                      ui::MessageType::INFO);
                }
                else if (error_code == "ALREADY_HAS_ADMIN")
                {
                    ui::print_message("Broker", "Admin request error sent to " + requester_id + ": Topic already has admin",
                                      ui::MessageType::INFO);
                }
                else
                {
                    ui::print_message("Broker", "Admin request error sent to " + requester_id + ": " + error_message,
                                      ui::MessageType::INFO);
                }
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "Error sending admin request error: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }
        else
        {
            ui::print_message("Broker", "Requester " + requester_id + " not connected for error notification",
                              ui::MessageType::INFO);
        }
    }

    void Broker::publish(const std::string &topic, const std::vector<uint8_t> &message)
    {
        // Verificar si es un mensaje administrativo
        try
        {
            std::string msg_str(message.begin(), message.end());

            // Manejo de marcar sensor como activable
            if (msg_str.find("__admin_sensor_activable") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__admin_sensor_activable") && json["__admin_sensor_activable"].get<bool>())
                {
                    std::string topic_name = json["topic_name"];
                    std::string sensor_name = json["sensor_name"];
                    bool activable = json["activable"];
                    std::string client_id = json["client_id"];

                    if (db_manager_)
                    {
                        bool ok = db_manager_->set_sensor_activable(topic_name, sensor_name, client_id, activable);
                        if (ok)
                        {
                            ui::print_message("Broker", "Sensor '" + sensor_name + "' en tópico '" + topic_name + "' marcado como activable=" + std::string(activable ? "true" : "false"), ui::MessageType::SUCCESS);
                        }
                        else
                        {
                            ui::print_message("Broker", "No se pudo marcar sensor activable (¿eres dueño?): " + topic_name, ui::MessageType::WARNING);
                        }
                    }
                    return; // No distribuir como publicación normal
                }
            }

            // DETECCIÓN DE SOLICITUDES ADMINISTRATIVAS
            if (msg_str.find("__admin_request") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__admin_request") && json["__admin_request"].get<bool>())
                {
                    ui::print_message("Broker", "=== ADMIN REQUEST DETECTED ===", ui::MessageType::INFO);

                    std::string requester_id = json["client_id"].get<std::string>();
                    std::string topic_name = json["topic_name"].get<std::string>();
                    std::string owner_id = json["owner_id"].get<std::string>();

                    ui::print_message("Broker", "Requester: " + requester_id, ui::MessageType::INFO);
                    ui::print_message("Broker", "Topic: " + topic_name, ui::MessageType::INFO);
                    ui::print_message("Broker", "Owner: " + owner_id, ui::MessageType::INFO);

                    // Buscar la sesión del solicitante
                    std::shared_ptr<Session> requester_session = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(sessions_mutex_);
                        auto it = sessions_.find(requester_id);
                        if (it != sessions_.end())
                        {
                            requester_session = it->second;
                        }
                    }

                    if (requester_session && has_database())
                    {
                        // REALIZAR TODAS LAS VERIFICACIONES
                        // Verificar que el solicitante existe
                        if (!db_manager_->client_exists(requester_id))
                        {
                            ui::print_message("Broker", "❌ Requester does not exist in database", ui::MessageType::ERROR);
                            notify_admin_request_error(requester_id, topic_name, "REQUESTER_NOT_FOUND",
                                                       "El solicitante no existe en el sistema");
                            return;
                        }

                        // Verificar que el owner existe
                        if (!db_manager_->client_exists(owner_id))
                        {
                            ui::print_message("Broker", "❌ Owner does not exist in database", ui::MessageType::ERROR);
                            notify_admin_request_error(requester_id, topic_name, "OWNER_NOT_FOUND",
                                                       "El propietario del tópico no existe");
                            return;
                        }

                        // Verificar que el tópico existe
                        if (!db_manager_->topic_exists(topic_name))
                        {
                            ui::print_message("Broker", "❌ Topic does not exist", ui::MessageType::ERROR);
                            notify_admin_request_error(requester_id, topic_name, "TOPIC_NOT_FOUND",
                                                       "El tópico no existe");
                            return;
                        }

                        // Verificar que no es el mismo cliente
                        if (owner_id == requester_id)
                        {
                            ui::print_message("Broker", "❌ Cannot request admin for own topic", ui::MessageType::WARNING);
                            notify_admin_request_error(requester_id, topic_name, "SELF_REQUEST",
                                                       "No puedes solicitar administración de tu propio tópico");
                            return;
                        }

                        // Verificar que está suscrito al tópico
                        if (!db_manager_->is_client_subscribed(requester_id, topic_name))
                        {
                            ui::print_message("Broker", "❌ Requester not subscribed to topic", ui::MessageType::ERROR);
                            notify_admin_request_error(requester_id, topic_name, "NOT_SUBSCRIBED",
                                                       "Debes estar suscrito al tópico para solicitar administración");
                            return;
                        }

                        // Verificar que no hay administrador ya
                        if (db_manager_->topic_has_admin(topic_name))
                        {
                            ui::print_message("Broker", "❌ Topic already has administrator", ui::MessageType::WARNING);
                            notify_admin_request_error(requester_id, topic_name, "ALREADY_HAS_ADMIN",
                                                       "El tópico ya tiene un administrador asignado");
                            return;
                        }

                        // Procesar la solicitud
                        if (db_manager_->request_admin_status(topic_name, requester_id))
                        {
                            ui::print_message("Broker", "✅ Admin request registered in database", ui::MessageType::SUCCESS);

                            // Notificar al dueño
                            notify_admin_request(owner_id, topic_name, requester_id);
                            ui::print_message("Broker", "✅ Owner notification sent", ui::MessageType::SUCCESS);

                            // Enviar confirmación de éxito al solicitante
                            nlohmann::json success_response = {
                                {"__admin_request_success", true},
                                {"topic_name", topic_name},
                                {"message", "Solicitud de administración enviada correctamente"},
                                {"timestamp", std::time(nullptr)}};

                            std::string success_json = success_response.dump();
                            std::vector<uint8_t> payload(success_json.begin(), success_json.end());
                            Packet success_packet(PacketType::ADMIN_REQ_ACK, 0, payload); // flag 0 = éxito
                            requester_session->send_packet(success_packet);

                            // 🎉 MENSAJE DE ÉXITO COMPLETO
                            ui::print_message("Broker", "🎉 ADMIN REQUEST COMPLETED SUCCESSFULLY! 🎉", ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "📋 SUMMARY:", ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "   • Requester: " + requester_id, ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "   • Topic: " + topic_name, ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "   • Owner: " + owner_id, ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "   • Status: PENDING OWNER APPROVAL", ui::MessageType::SUCCESS);
                            ui::print_message("Broker", "==========================================", ui::MessageType::SUCCESS);
                        }
                        else
                        {
                            ui::print_message("Broker", "❌ Failed to register admin request", ui::MessageType::ERROR);
                            notify_admin_request_error(requester_id, topic_name, "DATABASE_ERROR",
                                                       "Error al registrar la solicitud en la base de datos");
                        }
                    }

                    ui::print_message("Broker", "=== END ADMIN REQUEST ===", ui::MessageType::INFO);
                    return; // No distribuir este mensaje como publicación normal
                }
            }

            // DETECCIÓN DE RESPUESTAS ADMINISTRATIVAS
            if (msg_str.find("__admin_response") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__admin_response") && json["__admin_response"].get<bool>())
                {
                    ui::print_message("Broker", "=== ADMIN RESPONSE DETECTED ===", ui::MessageType::INFO);

                    std::string owner_id = json["client_id"].get<std::string>();
                    std::string topic_name = json["topic_name"].get<std::string>();
                    std::string requester_id = json["requester_id"].get<std::string>();
                    bool approved = json["approved"].get<bool>();

                    ui::print_message("Broker", "Owner: " + owner_id + " -> " + (approved ? "APPROVED" : "DENIED"), ui::MessageType::INFO);
                    ui::print_message("Broker", "Topic: " + topic_name, ui::MessageType::INFO);
                    ui::print_message("Broker", "Requester: " + requester_id, ui::MessageType::INFO);

                    // Buscar la sesión del dueño
                    std::shared_ptr<Session> owner_session = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(sessions_mutex_);
                        auto it = sessions_.find(owner_id);
                        if (it != sessions_.end())
                        {
                            owner_session = it->second;
                        }
                    }

                    if (owner_session && has_database())
                    {
                        // Procesar la respuesta en la base de datos
                        if (db_manager_->respond_to_admin_request(topic_name, owner_id, requester_id, approved))
                        {
                            ui::print_message("Broker", "✅ Admin response processed in database", ui::MessageType::SUCCESS);

                            // Si fue aprobado, agregar a la tabla de administradores
                            if (approved)
                            {
                                if (db_manager_->add_topic_admin(topic_name, requester_id))
                                {
                                    ui::print_message("Broker", "✅ Admin privileges granted", ui::MessageType::SUCCESS);

                                    // 🎉 MENSAJE DE ÉXITO FINAL
                                    ui::print_message("Broker", "🎉 ADMINISTRATION REQUEST COMPLETED! 🎉", ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "📋 FINAL SUMMARY:", ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "   • New Administrator: " + requester_id, ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "   • Topic: " + topic_name, ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "   • Approved by: " + owner_id, ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "   • Status: ✅ ACTIVE ADMINISTRATOR", ui::MessageType::SUCCESS);
                                    ui::print_message("Broker", "==========================================", ui::MessageType::SUCCESS);
                                }
                                else
                                {
                                    ui::print_message("Broker", "❌ Failed to grant admin privileges", ui::MessageType::ERROR);
                                }
                            }
                            else
                            {
                                ui::print_message("Broker", "❌ ADMINISTRATION REQUEST REJECTED", ui::MessageType::INFO);
                                ui::print_message("Broker", "📋 SUMMARY:", ui::MessageType::INFO);
                                ui::print_message("Broker", "   • Requester: " + requester_id, ui::MessageType::INFO);
                                ui::print_message("Broker", "   • Topic: " + topic_name, ui::MessageType::INFO);
                                ui::print_message("Broker", "   • Rejected by: " + owner_id, ui::MessageType::INFO);
                                ui::print_message("Broker", "==========================================", ui::MessageType::INFO);
                            }

                            // Notificar el resultado al solicitante
                            notify_admin_result(requester_id, topic_name, approved, owner_id);
                            ui::print_message("Broker", "✅ Result notification sent to requester", ui::MessageType::SUCCESS);
                        }
                        else
                        {
                            ui::print_message("Broker", "❌ Failed to process admin response", ui::MessageType::ERROR);
                        }
                    }

                    ui::print_message("Broker", "=== END ADMIN RESPONSE ===", ui::MessageType::INFO);
                    return; // No distribuir este mensaje como publicación normal
                }
            }

            // NUEVO: Manejo de revocación de administrador
            if (msg_str.find("__admin_revoke") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__admin_revoke") && json["__admin_revoke"].get<bool>())
                {
                    std::string owner_id = json["client_id"];
                    std::string topic_name = json["topic_name"];
                    std::string admin_to_revoke = json["admin_to_revoke"];

                    ui::print_message("Broker", "Processing admin revocation: " + admin_to_revoke + " from " + topic_name, ui::MessageType::INFO);

                    if (has_database())
                    {
                        if (db_manager_->revoke_admin_status(topic_name, owner_id, admin_to_revoke))
                        {
                            ui::print_message("Broker", "✅ Admin privileges revoked successfully", ui::MessageType::SUCCESS);

                            // Notificar al administrador revocado
                            notify_admin_revocation(admin_to_revoke, topic_name, owner_id);
                        }
                        else
                        {
                            ui::print_message("Broker", "❌ Failed to revoke admin privileges", ui::MessageType::ERROR);
                        }
                    }
                    return; // No continuar con distribución normal
                }
            }

            // **NUEVO**: DETECCIÓN DE MENSAJES DE ACTUALIZACIÓN DE ESTADO DE PUBLICACIÓN
            if (msg_str.find("__topic_publish_update") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__topic_publish_update") && json["__topic_publish_update"].get<bool>())
                {
                    std::string client_id = json["client_id"];
                    std::string topic_name = json["topic_name"];
                    bool publish_state = json["publish"];

                    if (db_manager_)
                    {
                        db_manager_->set_topic_publish(topic_name, client_id, publish_state);
                        ui::print_message("Broker", "Topic publish state updated: " + topic_name + " = " + (publish_state ? "ON" : "OFF"), ui::MessageType::SUCCESS);
                    }
                    // NO continuar con distribución normal para mensajes administrativos
                    return;
                }
            }

            // **NUEVO**: DETECCIÓN DE MENSAJES DE CREACIÓN DE TÓPICO
            if (msg_str.find("__topic_create") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__topic_create") && json["__topic_create"].get<bool>())
                {
                    std::string client_id = json["client_id"];
                    std::string topic_name = json["topic_name"];

                    if (db_manager_)
                    {
                        db_manager_->register_topic(topic_name, client_id);
                        ui::print_message("Broker", "Topic created: " + topic_name + " by " + client_id,
                                          ui::MessageType::SUCCESS);
                    }
                    // NO continuar con distribución normal
                    return;
                }
            }

            // **NUEVO**: DETECCIÓN DE MENSAJES DE SINCRONIZACIÓN DE SENSORES
            if (msg_str.find("__topic_sensors_sync") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__topic_sensors_sync") && json["__topic_sensors_sync"].get<bool>())
                {
                    std::string client_id = json["client_id"];
                    std::string topic_name = json["topic_name"];
                    auto sensors = json["sensors"];

                    if (db_manager_)
                    {
                        // Aquí podrías agregar lógica para sincronizar sensores si es necesario
                        ui::print_message("Broker", "Sensors synchronized for topic: " + topic_name + " by " + client_id,
                                          ui::MessageType::SUCCESS);
                    }
                    // NO continuar con distribución normal
                    return;
                }
            }

            // DETECCIÓN DE MENSAJES DE CONTROL DE PUBLICACIÓN (código existente)
            if (msg_str.find("__topic_publish") != std::string::npos)
            {
                nlohmann::json json = nlohmann::json::parse(msg_str);

                if (json.contains("__topic_publish") && json["__topic_publish"].get<bool>())
                {
                    if (!has_database())
                    {
                        ui::print_message("Broker", "No se puede cambiar estado de publicación: base de datos no disponible",
                                          ui::MessageType::WARNING);
                        return;
                    }

                    std::string client_id = json["client_id"].get<std::string>();
                    std::string topic_name = json["topic_name"].get<std::string>();
                    bool publish = json["publish"].get<bool>();

                    // Extraer el ID del cliente publicador
                    std::string publisher_client_id = extract_client_id_from_topic(topic);

                    // Verificar que el cliente que envía el mensaje sea el propietario del tópico
                    if (!client_id.empty() && client_id == publisher_client_id)
                    {
                        bool success = db_manager_->set_topic_publish(topic_name, client_id, publish);
                        if (success)
                        {
                            ui::print_message("Broker",
                                              "Estado de publicación de tópico '" + topic_name +
                                                  "' cambiado a " + (publish ? "ACTIVO" : "INACTIVO") +
                                                  " por cliente " + client_id,
                                              ui::MessageType::SUCCESS);
                        }
                    }
                    else
                    {
                        ui::print_message("Broker", "Cliente no autorizado para cambiar estado de publicación",
                                          ui::MessageType::WARNING);
                    }

                    // No distribuir este mensaje
                    return;
                }
            }

            if (msg_str.find("command") != std::string::npos && msg_str.find("set_sensor") != std::string::npos)
            {
                ui::print_message("Broker", "=== SENSOR STATUS CHANGE DETECTED ===", ui::MessageType::INFO);

                // Detectar si este mensaje es una notificación interna para evitar bucles
                if (msg_str.find("__is_internal_notification") != std::string::npos)
                {
                    ui::print_message("Broker", "Ignorando notificación interna para evitar bucle recursivo", ui::MessageType::DEBUG);
                    return; // Salir temprano para romper el bucle
                }

                nlohmann::json json = nlohmann::json::parse(msg_str);
                std::string topic_name = json["topic_name"];
                std::string sensor_name = json["sensor_name"];
                bool active = json["active"];

                // AÑADIR: Extraer client_id del JSON si está disponible
                std::string client_id;
                if (json.contains("client_id"))
                {
                    client_id = json["client_id"];
                    ui::print_message("Broker", "Usando client_id del mensaje: " + client_id, ui::MessageType::DEBUG);
                }
                else
                {
                    // Si no hay client_id en el JSON, usar publisher_client_id
                    client_id = extract_client_id_from_topic(topic);
                    ui::print_message("Broker", "Comando sin client_id explícito, usando: " + client_id, ui::MessageType::INFO);
                }

                // Extraer owner_id desde topic_name
                std::string owner_id = extract_client_id_from_topic(topic_name);
                if (owner_id.empty())
                {
                    ui::print_message("Broker", "No se pudo extraer owner_id del topic_name, consultando en la BD", ui::MessageType::INFO);

                    // Consultar en la base de datos quién es el propietario del tópico
                    owner_id = db_manager_->get_topic_owner(topic_name);

                    if (!owner_id.empty())
                    {
                        ui::print_message("Broker", "Propietario obtenido de la BD: " + owner_id, ui::MessageType::INFO);
                    }
                    else
                    {
                        ui::print_message("Broker", "No se pudo determinar el propietario del tópico: " + topic_name, ui::MessageType::WARNING);
                    }
                }

                // Actualizar estado en la base de datos
                if (db_manager_)
                {
                    ui::print_message("Broker", "Llamando a set_sensor_status en DB...", ui::MessageType::INFO);
                    if (db_manager_->set_sensor_status(topic_name, sensor_name, client_id, active))
                    {
                        ui::print_message("Broker",
                                          "Sensor " + sensor_name + " " + (active ? "activado" : "desactivado") +
                                              " para tópico " + topic_name,
                                          ui::MessageType::SUCCESS);

                        // NUEVO: Notificar al solicitante (admin) que el cambio fue exitoso
                        ui::print_message("Broker", "Enviando SENSOR_STATUS_RESP a " + client_id, ui::MessageType::INFO);
                        nlohmann::json confirmation;
                        confirmation["__sensor_status_changed"] = true;
                        confirmation["topic_name"] = topic_name;
                        confirmation["sensor_name"] = sensor_name;
                        confirmation["active"] = active;
                        confirmation["success"] = true;
                        confirmation["timestamp"] = std::time(nullptr);

                        std::string confirmation_str = confirmation.dump();
                        std::vector<uint8_t> confirm_payload(confirmation_str.begin(), confirmation_str.end());

                        // Buscar la sesión del cliente que envió el comando
                        std::shared_ptr<Session> sender_session = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(sessions_mutex_);
                            auto it = sessions_.find(client_id); // client_id es el admin que envió el comando
                            if (it != sessions_.end())
                            {
                                sender_session = it->second;
                            }
                        }

                        // Enviar confirmación directamente al cliente que hizo el cambio
                        if (sender_session)
                        {
                            Packet confirm_packet(PacketType::SENSOR_STATUS_RESP, 0, confirm_payload);
                            sender_session->send_packet(confirm_packet);
                        }

                        // Buscar la sesión del propietario del sensor
                        if (!owner_id.empty())
                        {
                            std::shared_ptr<Session> owner_session = nullptr;
                            {
                                std::lock_guard<std::mutex> lock(sessions_mutex_);
                                auto it = sessions_.find(owner_id);
                                if (it != sessions_.end())
                                {
                                    owner_session = it->second;
                                }
                            }

                            if (owner_session)
                            {
                                ui::print_message("Broker", "Enviando comando directamente al ESP32: " + owner_id, ui::MessageType::INFO);

                                // Enviar comando directo como paquete especial (no usar publish recursivamente)
                                nlohmann::json cmd = {
                                    {"command", "set_sensor"},
                                    {"sensor_name", sensor_name},
                                    {"active", active},
                                    {"__is_internal_notification", true} // Marca especial para evitar bucles
                                };

                                std::string cmd_str = cmd.dump();
                                std::vector<uint8_t> payload(cmd_str.begin(), cmd_str.end());
                                Packet cmd_packet(PacketType::ADMIN_NOTIFY, 0, payload);
                                owner_session->send_packet(cmd_packet);
                            }
                            else
                            {
                                ui::print_message("Broker", "No se encontró sesión para el dueño: " + owner_id, ui::MessageType::WARNING);
                            }
                        }
                        else
                        {
                            ui::print_message("Broker", "No se pudo determinar el propietario del sensor", ui::MessageType::WARNING);
                        }
                    }
                    else
                    {
                        ui::print_message("Broker", "set_sensor_status falló en DB", ui::MessageType::WARNING);
                    }
                }

                return; // No distribuir como publicación normal
            }
        }

        catch (const std::exception &e)
        {
            // Si no se puede parsear como JSON, continuar con el procesamiento normal
            ui::print_message("Broker", "JSON parse warning (continuing): " + std::string(e.what()), ui::MessageType::INFO);
        }

        // ========================================
        // PROCESAMIENTO NORMAL DE PUBLICACIONES
        // ========================================

        std::vector<std::shared_ptr<Session>> subscribers;
        bool is_new_topic = false;
        {
            std::lock_guard<std::mutex> lock(topics_mutex_);
            auto it = topic_subscribers_.find(topic);
            if (it != topic_subscribers_.end())
            {
                subscribers = it->second;
            }
            else
            {
                // Si el tópico no existe en topic_subscribers_, es un nuevo tópico
                is_new_topic = true;
            }
        }

        // Extract client ID from the publisher (from topic format: [client_id]/topic_name)
        std::string publisher_client_id = extract_client_id_from_topic(topic);

        // Mostrar mensaje específico para tópicos nuevos
        if (is_new_topic)
        {
            ui::print_message("Broker", "Nuevo tópico creado: '" + topic + "' por cliente: " + publisher_client_id, ui::MessageType::SUCCESS);
        }

        // Log to database if enabled
        if (db_manager_ && !publisher_client_id.empty())
        {
            int topic_id = -1;

            // Check topic cache
            {
                std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                auto it = topic_id_cache_.find(topic);
                if (it != topic_id_cache_.end())
                {
                    topic_id = it->second;
                }
            }

            // If not in cache, try to get from DB
            if (topic_id == -1)
            {
                topic_id = db_manager_->get_topic_id(topic);

                // Register topic if it doesn't exist yet
                if (topic_id == -1)
                {
                    bool registrado = db_manager_->register_topic(topic, publisher_client_id);
                    if (registrado)
                    {
                        topic_id = db_manager_->get_topic_id(topic);
                        // Cache it
                        std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                        topic_id_cache_[topic] = topic_id;

                        ui::print_message("Database", "Tópico registrado en la base de datos: " + topic, ui::MessageType::SUCCESS);
                    }
                }
                else
                {
                    // Cache it
                    std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                    topic_id_cache_[topic] = topic_id;
                }
            }

            // Log the message
            if (topic_id != -1)
            {
                // Create a small preview of the message
                std::string preview;
                size_t preview_size = std::min(message.size(), static_cast<size_t>(20));
                if (preview_size > 0)
                {
                    preview = std::string(message.begin(), message.begin() + preview_size);
                    if (message.size() > 50)
                    {
                        preview += "...";
                    }
                }

                // Create a larger preview for DB (50 chars)
                std::string preview_large;
                size_t preview_large_size = std::min(message.size(), static_cast<size_t>(100));
                if (preview_large_size > 0)
                {
                    preview_large = std::string(message.begin(), message.begin() + preview_large_size);
                    if (message.size() > 100)
                    {
                        preview_large += "...";
                    }
                }

                db_manager_->log_message(publisher_client_id, topic_id, message.size(), preview_large);
            }
        }

        if (subscribers.empty())
        {
            if (!is_new_topic)
            {
                ui::print_message("Topic", "No subscribers for topic: " + topic, ui::MessageType::INFO);
            }
            return;
        }

        // Crear el paquete para enviar a los suscriptores
        nlohmann::json json_msg = {
            {"topic", topic},
            {"message", std::string(message.begin(), message.end())}};
        std::string json_str = json_msg.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet packet(PacketType::PUB, 0, payload);

        // Enviar a todos los suscriptores
        for (auto &subscriber : subscribers)
        {
            try
            {
                subscriber->send_packet(packet);
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "Error sending message to subscriber: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }

        ui::print_message("Session", "Message distributed to " + std::to_string(subscribers.size()) + " subscribers",
                          ui::MessageType::OUTGOING);
    }

    std::string Broker::extract_client_id_from_topic(const std::string &topic)
    {
        // Si el tópico está en formato JSON (comienza con '[' y contiene comillas)
        if (topic.size() >= 2 && topic.compare(0, 2, "[\"") == 0)
        {
            // Extraer el contenido entre las comillas
            size_t start = 2; // Saltar '["'
            size_t pos = topic.find('/', start);
            if (pos != std::string::npos)
            {
                return topic.substr(start, pos - start);
            }
        }
        else // Formato normal sin JSON
        {
            size_t pos = topic.find('/');
            if (pos != std::string::npos)
            {
                return topic.substr(0, pos);
            }
        }
        return ""; // No es un formato de tópico válido
    }
    void Broker::send_published_topics(std::shared_ptr<Session> session)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available - cannot get published topics",
                              ui::MessageType::WARNING);
            // Enviar respuesta vacía
            session->send_topic_list(std::vector<std::pair<std::string, std::string>>());
            return;
        }

        // Obtener los tópicos publicados
        auto topics = db_manager_->get_published_topics();

        ui::print_message("Broker", "Sending " + std::to_string(topics.size()) + " published topics to client: " + session->client_id(), ui::MessageType::INFO);

        // Enviar la respuesta al cliente
        session->send_topic_list(topics);
    }

    void Broker::notify_admin_request(const std::string &owner_id, const std::string &topic_name, const std::string &requester_id)
    {
        ui::print_message("Broker", "=== SENDING NOTIFICATION (FIXED V2) ===", ui::MessageType::INFO);
        ui::print_message("Broker", "To: " + owner_id, ui::MessageType::INFO);
        ui::print_message("Broker", "About: " + requester_id + " wants admin for " + topic_name, ui::MessageType::INFO);

        // Buscar la sesión del dueño del tópico
        std::shared_ptr<Session> owner_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(owner_id);
            if (it != sessions_.end())
            {
                owner_session = it->second;
                ui::print_message("Broker", "✅ Found owner session", ui::MessageType::SUCCESS);
            }
            else
            {
                ui::print_message("Broker", "❌ Owner not connected", ui::MessageType::WARNING);
            }
        }

        if (owner_session)
        {
            try
            {
                // Crear un mensaje de notificación
                nlohmann::json notification = {
                    {"__admin_notification", true},
                    {"type", "request"},
                    {"requester_id", requester_id},
                    {"topic_name", topic_name},
                    {"timestamp", std::time(nullptr)}};

                std::string notification_json = notification.dump();
                ui::print_message("Broker", "Notification JSON: " + notification_json, ui::MessageType::INFO);

                // CORRECCIÓN CRÍTICA: Usar el formato con corchetes que los clientes esperan
                std::string notification_topic = "";
                if (!owner_id.empty())
                {
                    notification_topic = owner_id + "/admin_notifications";
                    ui::print_message("Broker", "Usando tópico de notificaciones: " + notification_topic, ui::MessageType::DEBUG);
                }
                else
                {
                    ui::print_message("Broker", "Owner ID no disponible, no se puede enviar notificación", ui::MessageType::WARNING);
                    return; // No continuar si no hay owner_id
                }
                ui::print_message("Broker", "Notification topic (FIXED): " + notification_topic, ui::MessageType::INFO);

                // DEPURACIÓN: Mostrar todos los tópicos suscritos
                {
                    std::lock_guard<std::mutex> lock(topics_mutex_);
                    ui::print_message("Broker", "=== DEBUG: ALL SUBSCRIBED TOPICS ===", ui::MessageType::INFO);
                    for (const auto &[topic, subscribers] : topic_subscribers_)
                    {
                        ui::print_message("Broker", "Topic: '" + topic + "' has " + std::to_string(subscribers.size()) + " subscribers", ui::MessageType::INFO);
                    }
                    ui::print_message("Broker", "=== END DEBUG ===", ui::MessageType::INFO);
                }

                // Verificar si hay suscriptores al tópico de notificaciones
                std::vector<std::shared_ptr<Session>> subscribers;
                {
                    std::lock_guard<std::mutex> lock(topics_mutex_);
                    auto it = topic_subscribers_.find(notification_topic);
                    if (it != topic_subscribers_.end())
                    {
                        subscribers = it->second;
                        ui::print_message("Broker", "✅ Found " + std::to_string(subscribers.size()) + " subscribers to notification topic", ui::MessageType::SUCCESS);
                    }
                    else
                    {
                        ui::print_message("Broker", "❌ No subscribers to notification topic: " + notification_topic, ui::MessageType::WARNING);
                        ui::print_message("Broker", "💡 Owner should subscribe to: " + notification_topic, ui::MessageType::INFO);
                    }
                }

                // Enviar la notificación
                if (!subscribers.empty())
                {
                    // Crear mensaje en formato wrapper para compatibilidad
                    nlohmann::json wrapper = {
                        {"topic", notification_topic},
                        {"message", notification_json}};
                    std::string wrapper_json = wrapper.dump();
                    std::vector<uint8_t> wrapped_payload(wrapper_json.begin(), wrapper_json.end());

                    Packet notification_packet(PacketType::PUB, 0, wrapped_payload);

                    for (auto &session : subscribers)
                    {
                        try
                        {
                            session->send_packet(notification_packet);
                            ui::print_message("Broker", "✅ Notification sent to subscriber", ui::MessageType::SUCCESS);
                        }
                        catch (const std::exception &e)
                        {
                            ui::print_message("Broker", "❌ Error sending notification: " + std::string(e.what()),
                                              ui::MessageType::ERROR);
                        }
                    }
                    ui::print_message("Broker", "🎉 NOTIFICATIONS SENT SUCCESSFULLY! 🎉", ui::MessageType::SUCCESS);
                }
                else
                {
                    ui::print_message("Broker", "⚠️ No subscribers for notifications - request saved in DB only",
                                      ui::MessageType::WARNING);
                }
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "❌ Error creating notification: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }

        ui::print_message("Broker", "=== END NOTIFICATION (FIXED V2) ===", ui::MessageType::INFO);
    }

    void Broker::send_my_topics(std::shared_ptr<Session> session)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "No database configured for my topics request",
                              ui::MessageType::WARNING);
            return;
        }

        const std::string &client_id = session->client_id();

        // Obtener los tópicos del cliente
        auto my_topics = db_manager_->get_my_topics(client_id);

        ui::print_message("Broker", "Sending " + std::to_string(my_topics.size()) + " own topics to client: " + client_id, ui::MessageType::INFO);

        // Serializar como JSON
        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &topic : my_topics)
        {
            nlohmann::json topic_obj;
            topic_obj["name"] = topic.at("name");
            topic_obj["publish_active"] = topic.at("publish_active") == "true";
            topic_obj["admin_client_id"] = topic.at("admin_client_id");
            topic_obj["created_at"] = topic.at("created_at");
            json_array.push_back(topic_obj);
        }

        std::string json_str = json_array.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet response_packet(PacketType::MY_TOPICS_RESP, 0, payload);
        session->send_packet(response_packet);
    }

    void Broker::notify_admin_revocation(const std::string &revoked_admin_id, const std::string &topic_name,
                                         const std::string &owner_id)
    {
        ui::print_message("Broker", "Notifying admin revocation to: " + revoked_admin_id, ui::MessageType::INFO);

        // Buscar la sesión del administrador revocado
        std::shared_ptr<Session> admin_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(revoked_admin_id);
            if (it != sessions_.end())
            {
                admin_session = it->second;
            }
        }

        if (admin_session)
        {
            try
            {
                // Crear mensaje de notificación de revocación
                nlohmann::json revocation_msg = {
                    {"__admin_revoked", true},
                    {"topic_name", topic_name},
                    {"owner_id", owner_id},
                    {"revoked_admin", revoked_admin_id},
                    {"timestamp", std::time(nullptr)}};

                std::string revocation_json = revocation_msg.dump();
                std::vector<uint8_t> payload(revocation_json.begin(), revocation_json.end());
                Packet revocation_packet(PacketType::ADMIN_RESULT, 0, payload);

                admin_session->send_packet(revocation_packet);

                ui::print_message("Broker", "✅ Admin revocation notification sent to " + revoked_admin_id, ui::MessageType::SUCCESS);
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker", "❌ Failed to send revocation notification: " + std::string(e.what()), ui::MessageType::ERROR);
            }
        }
        else
        {
            ui::print_message("Broker", "⚠️ Revoked admin " + revoked_admin_id + " not currently connected", ui::MessageType::WARNING);
        }
    }

    // Añadir al final del archivo o antes del último cierre de namespace

    void Broker::send_my_admin_requests(std::shared_ptr<Session> session)
    {
        if (!has_database())
        {
            ui::print_message("Broker", "Database not available - cannot get client admin requests", ui::MessageType::WARNING);

            // Enviar respuesta vacía
            std::vector<uint8_t> empty_payload = std::vector<uint8_t>(2, 0); // JSON array vacío "[]"
            Packet response_packet(PacketType::MY_ADMIN_RESP, 0, empty_payload);
            session->send_packet(response_packet);
            return;
        }

        const std::string &client_id = session->client_id();

        // Obtener solicitudes enviadas por este cliente
        auto requests = db_manager_->get_client_admin_requests(client_id);

        ui::print_message("Broker", "Sending " + std::to_string(requests.size()) + " admin requests made by client: " + client_id, ui::MessageType::INFO);

        // Serializar como JSON
        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &request : requests)
        {
            nlohmann::json req_obj;
            req_obj["id"] = std::stoi(request.at("id"));
            req_obj["topic_name"] = request.at("topic_name");
            req_obj["owner_id"] = request.at("owner_client_id");
            req_obj["status"] = request.at("status");
            req_obj["request_timestamp"] = std::stoll(request.at("request_time"));
            json_array.push_back(req_obj);
        }

        std::string json_str = json_array.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet response_packet(PacketType::MY_ADMIN_RESP, 0, payload);
        session->send_packet(response_packet);
    }
} // namespace tinymq