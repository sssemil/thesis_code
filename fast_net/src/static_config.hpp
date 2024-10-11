#pragma once

#include <netinet/in.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

class Config {
 public:
  static std::string host;
  static in_port_t port;
  static size_t num_requests;
  static std::string logging_level;
  static size_t page_count;
  static size_t client_threads;

  static void load_config(const std::string& env_file_path) {
    std::ifstream env_file(env_file_path.data());
    std::string line;

    if (env_file.is_open()) {
      while (std::getline(env_file, line)) {
        parse_env_line(line);
      }
      env_file.close();
    }

    host = get_env_var("HOST", host);
    port = static_cast<in_port_t>(std::stoul(get_env_var("PORT", std::to_string(port))));
    num_requests = std::stoul(get_env_var("NUM_REQUESTS", std::to_string(num_requests)));
    logging_level = get_env_var("LOGGING_LEVEL", logging_level);
    page_count = std::stoul(get_env_var("PAGE_COUNT", std::to_string(page_count)));
    client_threads = std::stoul(get_env_var("CLIENT_THREADS", std::to_string(client_threads)));

    set_logging_level();

    if (port == 0 || num_requests == 0) {
      throw std::runtime_error("Invalid configuration values.");
    }
  }

  static void load_config() {
    const std::string env_file_path = ".env";
    load_config(env_file_path);
  }

 private:
  static std::string trim(const std::string& str) {
    const size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
  }

  static void parse_env_line(const std::string& line) {
    if (const size_t delimiter_pos = line.find('=');
        delimiter_pos != std::string::npos) {
      const std::string key = trim(line.substr(0, delimiter_pos));
      const std::string value = trim(line.substr(delimiter_pos + 1));

      if (key == "HOST") {
        host = value;
      } else if (key == "PORT") {
        port = static_cast<in_port_t>(std::stoul(value));
      } else if (key == "NUM_REQUESTS") {
        num_requests = std::stoul(value);
      } else if (key == "LOGGING_LEVEL") {
        logging_level = value;
      } else if (key == "PAGE_COUNT") {
        page_count = std::stoul(value);
      } else if (key == "CLIENT_THREADS") {
        client_threads = std::stoul(value);
      } else {
        spdlog::warn("Unknown key '{}'.", key);
      }
    }
  }

  static std::string get_env_var(const std::string& key, const std::string& default_value) {
    const char* value = std::getenv(key.c_str());
    return value ? value : default_value;
  }

  static void set_logging_level() {
    if (logging_level == "DEBUG") {
      spdlog::set_level(spdlog::level::debug);
    } else if (logging_level == "INFO") {
      spdlog::set_level(spdlog::level::info);
    } else if (logging_level == "WARN") {
      spdlog::set_level(spdlog::level::warn);
    } else if (logging_level == "ERROR") {
      spdlog::set_level(spdlog::level::err);
    } else if (logging_level == "CRITICAL") {
      spdlog::set_level(spdlog::level::critical);
    } else if (logging_level == "OFF") {
      spdlog::set_level(spdlog::level::off);
    } else {
      spdlog::warn("Unknown LOGGING_LEVEL '{}'. Defaulting to INFO.",
                   logging_level);
      spdlog::set_level(spdlog::level::info);
    }
  }
};

std::string Config::host = "localhost";
in_port_t Config::port = 8888;
size_t Config::num_requests = 10;
std::string Config::logging_level = "DEBUG";
size_t Config::page_count = 1024;
size_t Config::client_threads = 4;