cmake_minimum_required(VERSION 3.28)
project(dpdk_seastar_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if (DEFINED PAGE_SIZE)
    add_definitions(-DPAGE_SIZE=${PAGE_SIZE})
endif ()

if (DEFINED SERVER_ADDR)
    add_definitions(-DSERVER_ADDR=${SERVER_ADDR})
endif ()

if (DEFINED PORT)
    add_definitions(-DPORT=${PORT})
endif ()

if (DEFINED NUM_REQUESTS)
    add_definitions(-DNUM_REQUESTS=${NUM_REQUESTS})
endif ()

if (DEFINED CLIENT_THREADS)
    add_definitions(-DCLIENT_THREADS=${CLIENT_THREADS})
endif ()

set(SEASTAR_ENABLE_DPDK ON)

include_directories(./seastar/include)
link_directories(./seastar/build/release/seastar)
add_subdirectory(seastar)

add_executable(simple_dpkd_server src/simple_dpkd_server.cpp
        src/simple_consts.hpp)
add_executable(simple_dpkd_client src/simple_dpkd_client.cpp
        src/simple_consts.hpp)

target_link_libraries(simple_dpkd_server
        seastar
        ${DPDK_LIBRARIES}
)
target_link_libraries(simple_dpkd_client
        seastar
        ${DPDK_LIBRARIES}
)
