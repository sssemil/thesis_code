#pragma once

#ifndef PAGE_SIZE
#define PAGE_SIZE 8*1024
#endif

#ifndef PORT
#define PORT 12346
#endif

#ifndef RING_SIZE
#define RING_SIZE 32
#endif

#ifndef NUM_REQUESTS
#define NUM_REQUESTS (1024 * 1024)
#endif

#ifndef SERVER_ADDR
#define SERVER_ADDR "127.0.0.1"
#endif

#ifndef VERIFY
#define VERIFY 0
#endif

#ifndef VERBOSE
#define VERBOSE 0
#endif

#ifndef CLIENT_THREADS
#define CLIENT_THREADS 1
#endif

#ifndef ALLOCATE_MALLOC
#define ALLOCATE_MALLOC 0
#endif

#ifndef BUFFER_POOL_INITIAL_POOL_SIZE
#define BUFFER_POOL_INITIAL_POOL_SIZE 32
#endif

#ifndef ALLOCATE_PIN
#define ALLOCATE_PIN 1
#endif

#ifndef ALLOCATE_REGISTERED_BUFFERS
#define ALLOCATE_REGISTERED_BUFFERS 1
#endif

#ifndef STUPID_BUFFER_MODE
#define STUPID_BUFFER_MODE 1
#endif

struct RequestData {
  size_t seq[2];
  int event_type;
  ssize_t buffer_offset;
  size_t registered_buffer_index;
  size_t buffer_size;
  int32_t buffer[];
};

enum EventType { READ_EVENT, WRITE_EVENT, SEND_EVENT, RECV_EVENT };
