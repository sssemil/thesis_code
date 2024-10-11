import os
import subprocess
import time
import csv
import re
import multiprocessing
import math as m

# page_sizes = [2 ** x for x in range(4, 14 + 1, 1)]
# ring_sizes = [x for x in range(128, 1024 + 1, 128)]
# client_threads = [2 ** x for x in
#                   range(0, int(m.log(os.cpu_count()) / m.log(2)) + 2)]
page_sizes = [16, 512, 4096]
ring_sizes = [8, 64, 128]
client_threads = [32, 64, 128]
num_requests = 1024 * 1024
initial_port = 12348

print(f"Page sizes: {page_sizes}")
print(f"Ring sizes: {ring_sizes}")
print(f"Client threads: {client_threads}")
experiment_count = len(page_sizes) * len(ring_sizes) * len(client_threads)
print(f"Running {experiment_count} experiments...")
time.sleep(5)


def run_server(build_dir, config):
  while True:
    server_process = subprocess.Popen(["./simple_iou_server"], cwd=build_dir,
                                      stderr=subprocess.PIPE, text=True)
    time.sleep(1)
    stderr = server_process.stderr.read()
    if "bind failed" in stderr:
      server_process.kill()
      server_process.wait()
      # config['PORT'] += 1
      continue
    return server_process


def build_and_run_server(config, build_dir="build"):
  subprocess.run(["mkdir", "-p", build_dir])
  cmake_command = ["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"] + [
    f'-D{key}={value}' for key, value in config.items()]
  subprocess.run(cmake_command, cwd=build_dir)
  subprocess.run(["make", "-j32"], cwd=build_dir)

  server_process = multiprocessing.Process(target=run_server,
                                           args=(build_dir, config))
  server_process.start()

  print("Waiting for server to finish...")
  server_process.join()
  print("Server terminated.")


with open('experiment_results.csv', 'w', newline='') as file:
  experiment_num = 0
  for page_size in page_sizes:
    for ring_size in ring_sizes:
      for client_thread in client_threads:
        print(
            f" ### [{experiment_num}] Running experiment with PAGE_SIZE={page_size}, RING_SIZE={ring_size}, NUM_REQUESTS={num_requests}, CLIENT_THREADS={client_thread}")
        port = initial_port
        initial_port += 1
        config = {'PAGE_SIZE': page_size, 'RING_SIZE': ring_size,
                  'NUM_REQUESTS': num_requests, 'CLIENT_THREADS': client_thread,
                  'PORT': port}
        build_and_run_server(config)
        experiment_num += 1
