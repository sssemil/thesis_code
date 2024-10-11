import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# data = pd.read_csv('results/small_grid_e2e_private_2.csv')
# data = pd.read_csv('results/small_grid_search_e2e_c6in32.csv')
data = pd.read_csv('results/large_grid_e2e_c6in32.csv')

# Convert PAGE_SIZE from words to bytes (1 word = 4 bytes)
data['PAGE_SIZE_BYTES'] = data['PAGE_SIZE'] * 4

# Aggregate data to reduce noise
data_aggregated = data.groupby(['PAGE_SIZE_BYTES', 'CLIENT_THREADS']).mean().reset_index()

def create_2d_plots(data, value_column, title, y_label):
  fig, ax = plt.subplots(figsize=(10, 10))

  threads = sorted(data['CLIENT_THREADS'].unique())
  colors = plt.cm.viridis(np.linspace(0, 1, len(threads)))

  markers = ['o', 's', '^', 'D', 'P', '*', 'X', 'v', '<', '>']
  marker_cycle = len(markers)

  for idx, (c, t) in enumerate(zip(colors, threads)):
      subset = data[data['CLIENT_THREADS'] == t]
      ax.plot(subset['PAGE_SIZE_BYTES'], subset[value_column], marker=markers[idx % marker_cycle], linestyle='-', color=c, alpha=0.6, label=f'Threads: {t}')

  ax.set_xlabel('Page Size (bytes)')
  ax.set_ylabel(y_label)
  ax.set_title(title)
  ax.set_xscale('log')  # Use log scale for better visualization of spread
  # ax.set_yscale('log')  # Use log scale for better visualization of spread
  ax.grid(True, which="both", linestyle='--', linewidth=0.5)

  # Move legend outside the plot



  return fig, ax

# Create 2D plots for both Average Rate and Average Gbps using aggregated data
fig_rate, ax_rate = create_2d_plots(data_aggregated, 'AverageRate(it/s)',
                                  'Average Rate (messages per second)',
                                  'Average Rate (messages per second)')
ax_rate.legend(loc='upper right', fontsize='small', ncol=1, frameon=False)

fig_gbps, ax_gbps = create_2d_plots(data_aggregated, 'AverageGbps',
                                  'Average Throughput (Gbit/s)',
                                  'Average Throughput (Gbit/s)')
ax_gbps.legend(loc='upper left', fontsize='small', ncol=1, frameon=False)

plt.show()

# Print best config for each Client Threads
print("# Best config for each Client Threads based on AverageGbps:")
print(data.loc[data.groupby('CLIENT_THREADS')['AverageGbps'].idxmax()])

# Print best config for each Page Size
print("# Best config for each Page Size based on AverageGbps:")
print(data.loc[data.groupby('PAGE_SIZE_BYTES')['AverageGbps'].idxmax()])
