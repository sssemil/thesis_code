cmake_minimum_required(VERSION 3.22)
project(fast_net)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")

# Manual inclusion of liburing
include_directories(libs/liburing/src/include)
link_directories(libs/liburing/src)

set(PROJECT_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

file(GLOB_RECURSE SOURCE_FILES "${PROJECT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADER_FILES "${PROJECT_SOURCE_DIR}/*.hpp")
list(REMOVE_ITEM SOURCE_FILES "${PROJECT_SOURCE_DIR}/server.cpp")
list(REMOVE_ITEM SOURCE_FILES "${PROJECT_SOURCE_DIR}/client.cpp")
list(REMOVE_ITEM SOURCE_FILES "${PROJECT_SOURCE_DIR}/server_s.cpp")
list(REMOVE_ITEM SOURCE_FILES "${PROJECT_SOURCE_DIR}/client_s.cpp")

add_executable(server "${PROJECT_SOURCE_DIR}/server.cpp" ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(server PRIVATE uring)

add_executable(client "${PROJECT_SOURCE_DIR}/client.cpp" ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(client PRIVATE uring)

add_executable(server_s "${PROJECT_SOURCE_DIR}/server_s.cpp" ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(server_s PRIVATE uring)

add_executable(client_s "${PROJECT_SOURCE_DIR}/client_s.cpp" ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(client_s PRIVATE uring)

add_custom_target(
        format
        COMMAND find ${CMAKE_SOURCE_DIR} -type f \( -iname "*.hpp" -o -iname "*.cpp" \) -exec clang-format -i {} +
)
