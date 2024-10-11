#pragma once

#include <string>

struct Config
{
    std::string server_addr;
    int queue_depth;
    int page_size;
    int thread_count;
    int connections_per_thread;
    int inflight_ops;
    int port;
    bool verbose;
    bool alloc_pin;
    bool enable_nagle;
    bool increase_socket_buffers;
    bool use_aligned_allocations;
    bool half_duplex_mode;
    bool pin_threads;
    int run_duration_seconds;

    void load_from_env();

    void save_to_file(const std::string& filepath) const;
};

extern Config config;
