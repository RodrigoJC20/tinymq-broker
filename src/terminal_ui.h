#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>

namespace tinymq
{
    namespace ui
    {

        const std::string RESET = "\033[0m";
        const std::string BLACK = "\033[30m";
        const std::string RED = "\033[31m";
        const std::string GREEN = "\033[32m";
        const std::string YELLOW = "\033[33m";
        const std::string BLUE = "\033[34m";
        const std::string MAGENTA = "\033[35m";
        const std::string CYAN = "\033[36m";
        const std::string WHITE = "\033[37m";
        const std::string BOLD = "\033[1m";

        enum class MessageType
        {
            INFO,
            SUCCESS,
            WARNING,
            ERROR,
            INCOMING,
            OUTGOING,
            SYSTEM,
            DEBUG
        };

        inline std::string get_timestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);

            std::stringstream ss;
            ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");
            return ss.str();
        }

        inline void print_message(const std::string &source, const std::string &message, MessageType type = MessageType::INFO)
        {
            std::string color;
            std::string prefix;

            switch (type)
            {
            case MessageType::INFO:
                color = BLUE;
                prefix = "INFO";
                break;
            case MessageType::SUCCESS:
                color = GREEN;
                prefix = "SUCCESS";
                break;
            case MessageType::WARNING:
                color = YELLOW;
                prefix = "WARNING";
                break;
            case MessageType::ERROR:
                color = RED;
                prefix = "ERROR";
                break;
            case MessageType::INCOMING:
                color = CYAN;
                prefix = "INCOMING";
                break;
            case MessageType::OUTGOING:
                color = MAGENTA;
                prefix = "OUTGOING";
                break;
            case MessageType::SYSTEM:
                color = WHITE + BOLD;
                prefix = "SYSTEM";
                break;
            }

            std::cout << color << "[" << get_timestamp() << "] [" << prefix << "] "
                      << "[" << source << "] " << message << RESET << std::endl;
        }

        inline void print_divider()
        {
            std::cout << BLUE << "----------------------------------------" << RESET << std::endl;
        }

        inline void print_header(const std::string &app_name, const std::string &version = "0.1.0")
        {
            print_divider();
            std::cout << BOLD << CYAN << "  " << app_name << " v" << version << RESET << std::endl;
            print_divider();
        }

    } // namespace ui
} // namespace tinymq