import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys

# Load the CSV file from the command line argument
data = pd.read_csv(sys.argv[1])

# Function to generate two separate 2D plots and save them as images
def create_and_save_separate_plots(data, filename_prefix):
    # Extracting unique values for threads, ring sizes, and page sizes
    client_threads = sorted(data['CLIENT_THREADS'].unique())
    ring_sizes = sorted(data['RING_SIZE'].unique())
    page_sizes = sorted(data['PAGE_SIZE'].unique())

    # Color palettes and line styles setup for differentiation
    color_palette_rate = plt.cm.spring(
        np.linspace(0, 1, len(client_threads) * len(ring_sizes)))
    color_palette_gbps = plt.cm.winter(
        np.linspace(0, 1, len(client_threads) * len(ring_sizes)))
    line_styles = ['-', '--', '-.', ':']

    # Plot for AverageRate(it/s)
    fig1, ax1 = plt.subplots(figsize=(12, 10))
    for thread_idx, thread_count in enumerate(client_threads):
        for ring_idx, ring_size in enumerate(ring_sizes):
            subset = data[(data['CLIENT_THREADS'] == thread_count) & (data['RING_SIZE'] == ring_size)]
            color_idx = thread_idx * len(ring_sizes) + ring_idx
            ax1.plot(subset['PAGE_SIZE'], subset['AverageRate(it/s)'], marker='o',
                     linestyle=line_styles[ring_idx % len(line_styles)],
                     color=color_palette_rate[color_idx],
                     label=f'Threads: {thread_count}, Ring Size: {ring_size}')

    ax1.set_title('Performance Plot for Average Rate')
    ax1.set_xlabel('Page Size (Bytes)')
    ax1.set_ylabel('Average Rate (it/s)')
    ax1.set_xticks(page_sizes)
    ax1.grid(True)

    # Adjust legend: place it outside the plot
    ax1.legend(title='Configurations', bbox_to_anchor=(1.05, 1), loc='upper left', fontsize='small', ncol=1)
    plt.tight_layout(rect=[0, 0, 0.85, 1])

    # Save the plot for AverageRate(it/s)
    fig1.savefig(f'{filename_prefix}_AverageRate.png')
    plt.close(fig1)

    # Plot for AverageGbps
    fig2, ax2 = plt.subplots(figsize=(12, 10))
    for thread_idx, thread_count in enumerate(client_threads):
        for ring_idx, ring_size in enumerate(ring_sizes):
            subset = data[(data['CLIENT_THREADS'] == thread_count) & (data['RING_SIZE'] == ring_size)]
            color_idx = thread_idx * len(ring_sizes) + ring_idx
            ax2.plot(subset['PAGE_SIZE'], subset['AverageGbps'], marker='o',
                     linestyle=line_styles[ring_idx % len(line_styles)],
                     color=color_palette_gbps[color_idx],
                     label=f'Threads: {thread_count}, Ring Size: {ring_size}')

    ax2.set_title('Performance Plot for Average Throughput (Gbps)')
    ax2.set_xlabel('Page Size (Bytes)')
    ax2.set_ylabel('Average Throughput (Gbps)')
    ax2.set_xticks(page_sizes)
    ax2.grid(True)

    # Adjust legend: place it outside the plot
    ax2.legend(title='Configurations', bbox_to_anchor=(1.05, 1), loc='upper left', fontsize='small', ncol=1)
    plt.tight_layout(rect=[0, 0, 0.85, 1])

    # Save the plot for AverageGbps
    fig2.savefig(f'{filename_prefix}_AverageGbps.png')
    plt.close(fig2)

# Extract the filename without the directory path or extension
filename_prefix = sys.argv[1].split('/')[-1].replace('.csv', '')

# Generate and save the plots
create_and_save_separate_plots(data, filename_prefix)

# Print best configuration for each Client Threads based on AverageGbps
print("# Optimal configuration for each number of Client Threads based on AverageGbps:")
print(data.loc[data.groupby('CLIENT_THREADS')['AverageGbps'].idxmax()])

# Print best configuration for each Page Size based on AverageGbps
print("# Optimal configuration for each Page Size based on AverageGbps:")
print(data.loc[data.groupby('PAGE_SIZE')['AverageGbps'].idxmax()])
