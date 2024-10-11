#include "static_config.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

Config config;

void Config::load_from_env()
{
    const char* env_server_addr = std::getenv("SERVER_ADDR");
    server_addr = env_server_addr ? env_server_addr : "127.0.0.1";

    const char* env_queue_depth = std::getenv("QUEUE_DEPTH");
    queue_depth = env_queue_depth ? std::stoi(env_queue_depth) : 512;

    const char* env_inflight_ops = std::getenv("INFLIGHT_OPS");
    inflight_ops = env_inflight_ops ? std::stoi(env_inflight_ops) : 256;

    const char* env_page_size = std::getenv("PAGE_SIZE");
    page_size = env_page_size ? std::stoi(env_page_size) : 4096;

    const char* env_thread_count = std::getenv("THREAD_COUNT");
    const char* env_connections_per_thread = std::getenv("CONNECTIONS_PER_THREAD");

    thread_count = env_thread_count ? std::stoi(env_thread_count) : 4;
    connections_per_thread = env_connections_per_thread ? std::stoi(env_connections_per_thread) : 4;

    queue_depth *= connections_per_thread;
    inflight_ops *= connections_per_thread;

    const char* env_port = std::getenv("PORT");
    port = env_port ? std::stoi(env_port) : 12345;

    const char* env_verbose = std::getenv("VERBOSE");
    verbose = env_verbose ? std::stoi(env_verbose) != 0 : false;

    const char* env_alloc_pin = std::getenv("ALLOC_PIN");
    alloc_pin = env_alloc_pin ? std::stoi(env_alloc_pin) != 0 : true;

    const char* env_enable_nagle = std::getenv("ENABLE_NAGLE");
    enable_nagle = env_enable_nagle ? std::stoi(env_enable_nagle) != 0 : true;

    const char* env_increase_socket_buffers = std::getenv("INCREASE_SOCKET_BUFFERS");
    increase_socket_buffers = env_increase_socket_buffers ? std::stoi(env_increase_socket_buffers) != 0 : false;

    const char* env_use_aligned_allocations = std::getenv("USE_ALIGNED_ALLOCATIONS");
    use_aligned_allocations = env_use_aligned_allocations ? std::stoi(env_use_aligned_allocations) != 0 : true;

    const char* env_half_duplex_mode = std::getenv("HALF_DUPLEX_MODE");
    half_duplex_mode = env_half_duplex_mode ? std::stoi(env_half_duplex_mode) != 0 : true;

    const char* env_pin_threads = std::getenv("PIN_THREADS");
    pin_threads = env_pin_threads ? std::stoi(env_pin_threads) != 0 : true;

    const char* env_run_duration_seconds = std::getenv("RUN_DURATION_SECONDS");
    run_duration_seconds = env_run_duration_seconds ? std::stoi(env_run_duration_seconds) : 60;

    printf("SERVER_ADDR: %s\n", server_addr.c_str());
    printf("QUEUE_DEPTH: %d\n", queue_depth);
    printf("INFLIGHT_OPS: %d\n", inflight_ops);
    printf("PAGE_SIZE: %d\n", page_size);
    printf("THREAD_COUNT: %d\n", thread_count);
    printf("CONNECTIONS_PER_THREAD: %d\n", connections_per_thread);
    printf("PORT: %d\n", port);
    printf("VERBOSE: %s\n", verbose ? "true" : "false");
    printf("ALLOC_PIN: %s\n", alloc_pin ? "true" : "false");
    printf("ENABLE_NAGLE: %s\n", enable_nagle ? "true" : "false");
    printf("INCREASE_SOCKET_BUFFERS: %s\n", increase_socket_buffers ? "true" : "false");
    printf("USE_ALIGNED_ALLOCATIONS: %s\n", use_aligned_allocations ? "true" : "false");
    printf("HALF_DUPLEX_MODE: %s\n", half_duplex_mode ? "true" : "false");
    printf("PIN_THREADS: %s\n", pin_threads ? "true" : "false");
    printf("RUN_DURATION_SECONDS: %d\n", run_duration_seconds);
}


void Config::save_to_file(const std::string& filepath) const
{
    std::ofstream ofs(filepath);
    if (!ofs)
    {
        std::cerr << "Error: Could not open file " << filepath << " for writing.\n";
        return;
    }

    ofs << "SERVER_ADDR=" << server_addr << "\n";
    ofs << "QUEUE_DEPTH=" << queue_depth << "\n";
    ofs << "INFLIGHT_OPS=" << inflight_ops << "\n";
    ofs << "PAGE_SIZE=" << page_size << "\n";
    ofs << "THREAD_COUNT=" << thread_count << "\n";
    ofs << "CONNECTIONS_PER_THREAD=" << connections_per_thread << "\n";
    ofs << "PORT=" << port << "\n";
    ofs << "VERBOSE=" << verbose << "\n";
    ofs << "ALLOC_PIN=" << alloc_pin << "\n";
    ofs << "ENABLE_NAGLE=" << enable_nagle << "\n";
    ofs << "INCREASE_SOCKET_BUFFERS=" << increase_socket_buffers << "\n";
    ofs << "USE_ALIGNED_ALLOCATIONS=" << use_aligned_allocations << "\n";
    ofs << "HALF_DUPLEX_MODE=" << half_duplex_mode << "\n";
    ofs << "PIN_THREADS=" << pin_threads << "\n";
    ofs << "RUN_DURATION_SECONDS=" << run_duration_seconds << "\n";

    ofs.close();

    if (ofs.fail())
    {
        std::cerr << "Error: Failed to write to file " << filepath << ".\n";
    }
    else
    {
        std::cout << "Configuration successfully saved to " << filepath << "\n";
    }
}
