import os
import subprocess
import time
import csv
import re
import multiprocessing
import math as m
import socket

# page_sizes = [2 ** x for x in range(4, 14 + 1, 1)]
# ring_sizes = [x for x in range(128, 1024 + 1, 128)]
# client_threads = [2 ** x for x in
#                   range(0, int(m.log(os.cpu_count()) / m.log(2)) + 2)]
page_sizes = [16, 512, 4096]
ring_sizes = [8, 64, 128]
client_threads = [32, 64, 128]
num_requests = 1024 * 1024
initial_port = 12348

server_addr = "127.0.0.1"
if os.getenv("SERVER_ADDR"):
  server_addr = os.getenv("SERVER_ADDR")
  print(f"Using SERVER_ADDR={server_addr} for server address.")
  server_addr = socket.gethostbyname_ex(server_addr)[2][0]
  print(f"Using SERVER_ADDR={server_addr} for server address.")

print(f"Page sizes: {page_sizes}")
print(f"Ring sizes: {ring_sizes}")
print(f"Client threads: {client_threads}")
experiment_count = len(page_sizes) * len(ring_sizes) * len(client_threads)
print(f"Running {experiment_count} experiments...")
time.sleep(5)


def build_and_run_client(config, build_dir="build"):
  subprocess.run(["mkdir", "-p", build_dir])
  cmake_command = ["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"] + [
    f'-D{key}={value}' for key, value in config.items()]
  cmake_command += [f"-DSERVER_ADDR=\"{server_addr}\""]
  subprocess.run(cmake_command, cwd=build_dir)
  subprocess.run(["make", "-j32"], cwd=build_dir)

  # wait a bit for the server to start
  time.sleep(2)
  client_output = subprocess.run(["./simple_iou_client"], cwd=build_dir,
                                 stdout=subprocess.PIPE, text=True)

  print(client_output.stdout)
  print(client_output.stderr)

  return client_output.stdout


def parse_output(output):
  rate_match = re.search(r'Average rate: (\d+\.\d+) it/s', output)
  gbps_match = re.search(r'Average Gbps: (\d+\.\d+)', output)
  return (rate_match.group(1) if rate_match else "N/A",
          gbps_match.group(1) if gbps_match else "N/A")


with open('experiment_results.csv', 'w', newline='') as file:
  writer = csv.writer(file)
  writer.writerow(['PAGE_SIZE', 'RING_SIZE', 'NUM_REQUESTS', 'CLIENT_THREADS',
                   'Average Rate (it/s)', 'Average Gbps'])

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
        output = build_and_run_client(config)
        rate, gbps = parse_output(output)
        writer.writerow(
            [page_size, ring_size, num_requests, client_thread, rate, gbps])
        experiment_num += 1

with open('experiment_results.csv', 'r') as file:
  for line in file:
    print(line, end='')
