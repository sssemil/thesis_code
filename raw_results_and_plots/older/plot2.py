import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# data = pd.read_csv('results/small_grid_e2e_private.csv')
# data = pd.read_csv('results/small_grid_e2e_private_2.csv')
# data = pd.read_csv('results/small_grid_e2e_c7gn_nv.csv')
# data = pd.read_csv('results/small_grid_e2e_private_2_post_buffer_pool.csv')

import sys
data = pd.read_csv(sys.argv[1])


def create_2d_combined_plots_with_labels(data, title):
  # Setup figure and subplots
  fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 16))

  # Unique values
  threads = sorted(data['CLIENT_THREADS'].unique())
  ring_sizes = sorted(data['RING_SIZE'].unique())
  page_sizes = sorted(data['PAGE_SIZE'].unique())

  # Colors and line styles setup
  color_palette_rate = plt.cm.spring(
    np.linspace(0, 1, len(threads) * len(ring_sizes)))
  color_palette_gbps = plt.cm.winter(
    np.linspace(0, 1, len(threads) * len(ring_sizes)))
  line_styles = ['-', '--', '-.', ':']

  # Plot AverageRate(it/s)
  for thread_idx, t in enumerate(threads):
    for ring_idx, r in enumerate(ring_sizes):
      subset = data[(data['CLIENT_THREADS'] == t) & (data['RING_SIZE'] == r)]
      color_idx = thread_idx * len(ring_sizes) + ring_idx
      ax1.plot(subset['PAGE_SIZE'], subset['AverageRate(it/s)'], marker='o',
               linestyle=line_styles[ring_idx % len(line_styles)],
               color=color_palette_rate[color_idx],
               label=f'Threads: {t}, Ring Size: {r}')

  ax1.set_title('2D Plot for Average Rate Across Ring Sizes')
  ax1.set_xlabel('PAGE_SIZE')
  ax1.set_ylabel('AverageRate(it/s)')
  ax1.set_xticks(page_sizes)
  ax1.grid(True)
  ax1.legend(title='Configurations', bbox_to_anchor=(1.05, 1), loc='upper left')

  # Plot AverageGbps
  for thread_idx, t in enumerate(threads):
    for ring_idx, r in enumerate(ring_sizes):
      subset = data[(data['CLIENT_THREADS'] == t) & (data['RING_SIZE'] == r)]
      color_idx = thread_idx * len(ring_sizes) + ring_idx
      ax2.plot(subset['PAGE_SIZE'], subset['AverageGbps'], marker='o',
               linestyle=line_styles[ring_idx % len(line_styles)],
               color=color_palette_gbps[color_idx],
               label=f'Threads: {t}, Ring Size: {r}')

  ax2.set_title('2D Plot for Average Gbps Across Ring Sizes')
  ax2.set_xlabel('PAGE_SIZE')
  ax2.set_ylabel('AverageGbps')
  ax2.set_xticks(page_sizes)
  ax2.grid(True)
  ax2.legend(title='Configurations', bbox_to_anchor=(1.05, 1), loc='upper left')

  # Adjust layout to make room for the legends outside the plot area
  plt.tight_layout(rect=[0, 0, 0.75, 1])
  plt.show()


create_2d_combined_plots_with_labels(data,
                                     'Combined 2D Plots for Performance Metrics')

# Print best config for each Client Threads based on AverageGbps
print("# Best config for each Client Threads based on AverageGbps:")
print(data.loc[data.groupby('CLIENT_THREADS')['AverageGbps'].idxmax()])

# Print best config for each Page Size based on AverageGbps
print("# Best config for each Page Size based on AverageGbps:")
print(data.loc[data.groupby('PAGE_SIZE')['AverageGbps'].idxmax()])
