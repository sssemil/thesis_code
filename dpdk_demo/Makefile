CC = gcc
CFLAGS = -I/usr/include/dpdk -I/usr/include/dpdk/../aarch64-linux-gnu/dpdk -include rte_config.h -march=armv8-a+crc -moutline-atomics -I/usr/include/aarch64-linux-gnu -I/usr/include/libnl3 -I/usr/include/dbus-1.0 -I/usr/lib/aarch64-linux-gnu/dbus-1.0/include
LDFLAGS = -Wl,--as-needed -lrte_node -lrte_graph -lrte_pipeline -lrte_table -lrte_pdump -lrte_port -lrte_fib -lrte_pdcp -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_mldev -lrte_regexdev -lrte_rawdev -lrte_power -lrte_pcapng -lrte_member -lrte_lpm -lrte_latencystats -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_gpudev -lrte_dispatcher -lrte_eventdev -lrte_efd -lrte_dmadev -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bpf -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_rcu -lrte_ring -lrte_eal -lrte_telemetry -lrte_kvargs -lrte_log -lbsd
SRC_SENDER = dpdk_sender.c
SRC_RECEIVER = dpdk_receiver.c
TARGET_SENDER = dpdk_sender
TARGET_RECEIVER = dpdk_receiver

.PHONY: all clean

all: $(TARGET_SENDER) $(TARGET_RECEIVER)

$(TARGET_SENDER): $(SRC_SENDER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_RECEIVER): $(SRC_RECEIVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET_SENDER) $(TARGET_RECEIVER)
