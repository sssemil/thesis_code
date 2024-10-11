
#include <sys/uio.h>

#define IO_URING_QUEUE_DEPTH 512

void setup_io_uring(struct io_uring& ring) {
  struct io_uring_params params {};
  memset(&params, 0, sizeof(params));
  params.flags = IORING_SETUP_SQPOLL;
  params.sq_thread_idle = 10000;

  int r = io_uring_queue_init_params(IO_URING_QUEUE_DEPTH, &ring, &params);
  if (r < 0) {
    spdlog::critical("Failed to initialize io_uring: {}", r);
    exit(1);
  }
}
