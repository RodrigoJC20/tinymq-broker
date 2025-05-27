#include "broker.h"
#include "terminal_ui.h"
#include <iostream>
#include <csignal>

tinymq::Broker *g_broker = nullptr;

void signal_handler(int signal)
{
    tinymq::ui::print_message("Signal", "Received signal " + std::to_string(signal) + ", shutting down...",
                              tinymq::ui::MessageType::WARNING);
    if (g_broker)
    {
        g_broker->stop();
    }
}

int main(int argc, char *argv[])
{
    uint16_t port = 1505;
    size_t thread_pool_size = 4;
    std::string db_connection_str;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
        {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--threads" && i + 1 < argc)
        {
            thread_pool_size = static_cast<size_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--db" && i + 1 < argc)
        {
            db_connection_str = argv[++i];
        }
        else if (arg == "--help")
        {
            std::cout << "TinyMQ Broker" << std::endl;
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --port PORT       Set the port number (default: 1505)" << std::endl;
            std::cout << "  --threads N       Set thread pool size (default: 4)" << std::endl;
            std::cout << "  --db CONNSTR      PostgreSQL connection string" << std::endl;
            std::cout << "                    Example: --db \"dbname=tinymq user=postgres password=secret\"" << std::endl;
            std::cout << "  --help            Show this help message" << std::endl;
            return 0;
        }
    }

    try
    {
        tinymq::ui::print_header("TinyMQ Broker");

        tinymq::ui::print_message("Config", "Port: " + std::to_string(port), tinymq::ui::MessageType::INFO);
        tinymq::ui::print_message("Config", "Thread pool size: " + std::to_string(thread_pool_size),
                                  tinymq::ui::MessageType::INFO);

        if (!db_connection_str.empty())
        {
            tinymq::ui::print_message("Config", "Database enabled", tinymq::ui::MessageType::INFO);
        }
        else
        {
            tinymq::ui::print_message("Config", "Database disabled", tinymq::ui::MessageType::WARNING);
        }

        tinymq::Broker broker(port, thread_pool_size, db_connection_str);
        g_broker = &broker;

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        tinymq::ui::print_message("Broker", "Starting broker...", tinymq::ui::MessageType::SYSTEM);
        broker.start();

        tinymq::ui::print_message("Broker", "Press Enter to stop the broker...", tinymq::ui::MessageType::SYSTEM);
        std::cin.get();

        tinymq::ui::print_message("Broker", "Stopping broker...", tinymq::ui::MessageType::SYSTEM);
        broker.stop();
        g_broker = nullptr;

        tinymq::ui::print_message("Broker", "Broker stopped successfully", tinymq::ui::MessageType::SUCCESS);
        return 0;
    }
    catch (const std::exception &e)
    {
        tinymq::ui::print_message("Broker", "Exception: " + std::string(e.what()), tinymq::ui::MessageType::ERROR);
        return 1;
    }
}