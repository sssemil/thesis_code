import matplotlib.pyplot as plt
import re

# Function to extract the bitrate from the iperf3 output file
def extract_bitrate(filename):
    bitrates = []
    with open(filename, 'r') as f:
        for line in f:
            # Regular expression to find lines with bitrate data
            match = re.search(r'\d+\.\d+\s+Gbits/sec', line)
            if match:
                # Extract and convert the bitrate to float
                bitrates.append(float(match.group(0).split()[0]))
    return bitrates

# List of file names and corresponding parallel streams
files_and_streams = [
    ("iperf3_output_P1.txt", 1),
    ("iperf3_output_P8.txt", 8),
    ("iperf3_output_P16.txt", 16),
    ("iperf3_output_P24.txt", 24),
    ("iperf3_output_P32.txt", 32),
    ("iperf3_output_P40.txt", 40),
    ("iperf3_output_P48.txt", 48),
    ("iperf3_output_P56.txt", 56),
    ("iperf3_output_P64.txt", 64)
]

# Initialize plot
plt.figure(figsize=(10, 6))

# Loop through each file and plot the bitrates
for file, streams in files_and_streams:
    bitrates = extract_bitrate(file)
    plt.plot(range(1, len(bitrates) + 1), bitrates, label=f"{streams} Streams")

# Add labels and legend
plt.title('iPerf3 Bandwidth vs Time for Different Parallel Streams')
plt.xlabel('Time (seconds)')
plt.ylabel('Bitrate (Gbits/sec)')
plt.legend()
plt.grid(True)

# Show the plot
plt.show()

