#pragma once

#include <pthread.h>
#include <sched.h>

#include <thread>
#include <iostream>


inline unsigned int get_num_cpus() {
    unsigned int n = std::thread::hardware_concurrency();
    if (n == 0) {
        // Fallback if hardware_concurrency cannot determine
        n = sysconf(_SC_NPROCESSORS_ONLN);
    }
    return n;
}

inline unsigned int get_cpu_for_thread(const int thread_id) {
    return thread_id % get_num_cpus();
}

inline bool set_thread_affinity(const int thread_id) {
    if (!config.pin_threads) {
        std::cout << "Thread Affinity is disabled in config!" << std::endl;
        return true;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    const unsigned int cpu = get_cpu_for_thread(thread_id);
    CPU_SET(cpu, &cpuset);

    const pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        std::cerr << "Error setting thread affinity: " << strerror(ret) << std::endl;
        return false;
    }

    // Optionally, verify the affinity was set correctly
    ret = pthread_getaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        std::cerr << "Error getting thread affinity: " << strerror(ret) << std::endl;
        return false;
    }

    // Print the CPUs the thread is allowed to run on
    std::cout << "Thread " << thread_id << " pinned to CPU(s): ";
    for (int j = 0; j < CPU_SETSIZE; j++) {
        if (CPU_ISSET(j, &cpuset)) {
            std::cout << j << " ";
        }
    }
    std::cout << std::endl;

    return true;
}
