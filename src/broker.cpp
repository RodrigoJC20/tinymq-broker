#include "broker.h"
#include "session.h"
#include "terminal_ui.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace tinymq
{

    Broker::Broker(uint16_t port, size_t thread_pool_size, const std::string &db_connection_str)
        : io_context_(),
          acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
          thread_pool_size_(thread_pool_size),
          running_(false)
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

    void Broker::stop()
    {
        if (!running_)
        {
            return;
        }

        running_ = false;

        acceptor_.close();

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

    void Broker::publish(const std::string &topic, const std::vector<uint8_t> &message)
    {
        // Verificar si es un mensaje especial para cambiar el estado de publicación
        try
        {
            std::string msg_str(message.begin(), message.end());
            if (msg_str.find("__topic_publish") != std::string::npos)
            { // Comprobación rápida
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

                    // No necesitamos distribuir este mensaje
                    return;
                }
            }
        }
        catch (...)
        {
            // Si no se puede parsear como JSON, continuar con el procesamiento normal
        }

        // Código existente para publicación normal
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
            { // Evitar mensaje duplicado
                ui::print_message("Topic", "No subscribers for topic: " + topic, ui::MessageType::INFO);
            }
            return;
        }

        nlohmann::json json_msg = {
            {"topic", topic},
            {"message", std::string(message.begin(), message.end())}
        };
        std::string json_str = json_msg.dump();
        std::vector<uint8_t> payload(json_str.begin(), json_str.end());
        Packet packet(PacketType::PUB, 0, payload);

        for (auto& subscriber : subscribers) {
             try
                {
                    subscriber->send_packet(packet);
                }
                catch (const std::exception &e)
                {
                    ui::print_message("Broker", "Error sending message to subscriber: " + std::string(e.what()), ui::MessageType::ERROR);
                }
        }
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
        // Buscar la sesión del dueño del tópico
        std::shared_ptr<Session> owner_session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(owner_id);
            if (it != sessions_.end())
            {
                owner_session = it->second;
            }
        }

        // Si el dueño está conectado, enviarle una notificación
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
                std::vector<uint8_t> payload(notification_json.begin(), notification_json.end());

                // El tópico para notificaciones de administración
                std::string notification_topic = owner_id + "/admin_notifications";

                // Publicar la notificación directamente a los suscriptores (si hay)
                std::vector<std::shared_ptr<Session>> subscribers;
                {
                    std::lock_guard<std::mutex> lock(topics_mutex_);
                    auto it = topic_subscribers_.find(notification_topic);
                    if (it != topic_subscribers_.end())
                    {
                        subscribers = it->second;
                    }
                }

                for (auto &session : subscribers)
                {
                    try
                    {
                        Packet packet(PacketType::PUB, 0, payload);
                        session->send_packet(packet);
                    }
                    catch (const std::exception &e)
                    {
                        ui::print_message("Broker",
                                          "Error al enviar notificación de solicitud: " + std::string(e.what()),
                                          ui::MessageType::ERROR);
                    }
                }

                ui::print_message("Broker",
                                  "Notificación de solicitud enviada a cliente: " + owner_id,
                                  ui::MessageType::INFO);
            }
            catch (const std::exception &e)
            {
                ui::print_message("Broker",
                                  "Error al crear notificación de solicitud: " + std::string(e.what()),
                                  ui::MessageType::ERROR);
            }
        }
        else
        {
            // El dueño no está conectado, guardar la solicitud solo en la base de datos
            ui::print_message("Broker",
                              "El dueño del tópico no está conectado, la solicitud quedará pendiente",
                              ui::MessageType::INFO);
        }
    }

} // namespace tinymq