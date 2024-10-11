#include "dpdk_common.h"
#include <rte_cycles.h>

static int udp_receive(uint16_t port_id) {
    struct rte_mbuf *rx_mbufs[BURST_SIZE];
    uint16_t nb_rx;
    uint64_t total_packets_received = 0;
    uint64_t end_tsc, total_tsc;
    uint64_t first_packet_tsc = 0;
    uint64_t total_bytes_received = 0;
    uint64_t total_bytes_with_headers_received = 0;

    printf("Starting UDP packet reception...\n");

    while (total_packets_received < NUM_ITERATIONS) {
        nb_rx = rte_eth_rx_burst(port_id, 0, rx_mbufs, BURST_SIZE);
        if (nb_rx == 0)
            continue;

        for (int i = 0; i < nb_rx; i++) {
            struct rte_mbuf *mbuf = rx_mbufs[i];
            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(mbuf,
            struct rte_ether_hdr *);

            if (likely(
                eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))) {
                struct rte_ipv4_hdr
                    *ip_hdr = (struct rte_ipv4_hdr *) (eth_hdr + 1);

                struct rte_udp_hdr
                    *udp_hdr = (struct rte_udp_hdr *) (ip_hdr + 1);
                uint32_t *data = (uint32_t * )(
                    (char *) udp_hdr + sizeof(struct rte_udp_hdr));
                uint32_t received_seq_num = rte_be_to_cpu_32(*data);

//                printf("Received packet with sequence number %u\n",
//                       received_seq_num);
                if (received_seq_num > NUM_ITERATIONS) {
                    printf("Sequence too large received %u\n",
                           received_seq_num);
                }

                // verify the packet data
                for (int i = 0; i < PAGE_SIZE; i++) {
                    if (data[i] != data[0]) {
                        printf(
                            "Data mismatch at index %d, expected %u, got %u\n",
                            i,
                            i,
                            data[i]);
                    }
                }

                total_bytes_received += PAGE_SIZE * sizeof(uint32_t);
                total_bytes_with_headers_received += sizeof(struct rte_ether_hdr)
                                                     + sizeof(struct rte_ipv4_hdr)
                                                     + sizeof(struct rte_udp_hdr)
                                                     + PAGE_SIZE * sizeof(uint32_t);

                if (unlikely(total_packets_received == 0)) {
                    first_packet_tsc = rte_rdtsc();
                }

                total_packets_received++;
            }

            rte_pktmbuf_free(mbuf);
        }
    }

    end_tsc = rte_rdtsc();
    total_tsc = end_tsc - first_packet_tsc;

    double seconds = (double) total_tsc / rte_get_tsc_hz();
    double throughput = (total_bytes_received * 8) / (seconds * 1000000000);
    double throughput_with_headers = (total_bytes_with_headers_received * 8) / (seconds * 1000000000);

    printf("Reception complete.\n");
    printf("Total time: %.2f seconds\n", seconds);
    printf("Total packets received: %lu\n", total_packets_received);
    printf("Total bytes received: %lu\n", total_bytes_received);
    printf("Throughput: %.2f Gbps\n", throughput);
    printf("Throughput with headers: %.2f Gbps\n", throughput_with_headers);

    return 0;
}

int main(int argc, char **argv) {
    uint16_t port_id = 0;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    struct rte_mempool
        *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                             NUM_MBUFS,
                                             MBUF_CACHE_SIZE,
                                             0,
                                             RTE_MBUF_DEFAULT_BUF_SIZE,
                                             rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    if (port_init(port_id, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"
    PRIu16
    "\n", port_id);

    udp_receive(port_id);

    return 0;
}