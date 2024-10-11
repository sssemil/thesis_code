#include "dpdk_common.h"
#include "dpdk_arp_utils.h"
#include <rte_arp.h>
#include <rte_cycles.h>

static inline int
prepare_packet(
    struct rte_mbuf *mbuf,
    struct rte_ether_addr *src_mac,
    struct rte_ether_addr *dst_mac,
    uint32_t src_ip,
    uint32_t dst_ip,
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t seq_num
) {
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(mbuf,
    struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *) (eth_hdr + 1);
    struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *) (ip_hdr + 1);
    uint32_t *data = (uint32_t * )(
        (char *) udp_hdr + sizeof(struct rte_udp_hdr));

    rte_ether_addr_copy(src_mac, &eth_hdr->src_addr);
    rte_ether_addr_copy(dst_mac, &eth_hdr->dst_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    ip_hdr->version_ihl = RTE_IPV4_VHL_DEF;
    ip_hdr->type_of_service = 0;
    ip_hdr->total_length = rte_cpu_to_be_16(
        sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr)
            + PAGE_SIZE * sizeof(uint32_t));
    ip_hdr->packet_id = 0;
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live = 64;
    // TODO: Compare TCP vs UDP throttling on AWS EC2, don't see any diff rn
    // How does AWS EC2 throttle TCP vs UDP?
    ip_hdr->next_proto_id = IPPROTO_TCP;
    ip_hdr->hdr_checksum = 0;
    ip_hdr->src_addr = src_ip;
    ip_hdr->dst_addr = dst_ip;

    udp_hdr->src_port = rte_cpu_to_be_16(src_port);
    udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
    udp_hdr->dgram_len =
        rte_cpu_to_be_16(
            sizeof(struct rte_udp_hdr) + PAGE_SIZE * sizeof(uint32_t));

    for (uint32_t i = 0; i < PAGE_SIZE; i++) {
        data[i] = rte_cpu_to_be_32(seq_num);
    }

    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);

    mbuf->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr)
        + sizeof(struct rte_udp_hdr) + PAGE_SIZE * sizeof(uint32_t);
    mbuf->pkt_len = mbuf->data_len;

    return 0;
}

int main(int argc, char **argv) {
    struct rte_mempool *mbuf_pool;
    uint16_t port_id = 0;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                        NUM_MBUFS,
                                        MBUF_CACHE_SIZE,
                                        0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE
                                            + PAGE_SIZE * sizeof(uint32_t),
                                        rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    if (port_init(port_id, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"
    PRIu16
    "\n", port_id);

    uint32_t src_ip = inet_addr(SRC_IP);
    uint32_t dst_ip = inet_addr(DST_IP);
    uint32_t gateway_ip;
    struct rte_ether_addr src_mac, dst_mac;

    if (get_default_gateway_ip(&gateway_ip) < 0)
        rte_exit(EXIT_FAILURE, "Failed to get default gateway IP\n");

    rte_eth_macaddr_get(port_id, &src_mac);

    if (send_arp_request(port_id, mbuf_pool, src_ip, gateway_ip, &dst_mac) < 0)
        rte_exit(EXIT_FAILURE, "Failed to resolve gateway MAC address\n");

    printf("Starting UDP packet transmission...\n");

    uint64_t start_tsc, end_tsc, total_tsc = 0;
    uint64_t total_bytes_sent = 0;
    uint64_t total_bytes_with_headers_received = 0;
    struct rte_mbuf *tx_mbufs[BURST_SIZE];

    for (int i = 0; i < NUM_ITERATIONS; i += BURST_SIZE) {
        start_tsc = rte_rdtsc();

        uint16_t nb_tx_burst = RTE_MIN(BURST_SIZE, NUM_ITERATIONS - i);

        for (int j = 0; j < nb_tx_burst; j++) {
            tx_mbufs[j] = rte_pktmbuf_alloc(mbuf_pool);
            if (tx_mbufs[j] == NULL) {
                printf("Failed to allocate mbuf\n");
                continue;
            }

            prepare_packet(tx_mbufs[j],
                           &src_mac,
                           &dst_mac,
                           src_ip,
                           dst_ip,
                           SRC_PORT,
                           DST_PORT,
                           i + j);
        }

        const uint16_t
            nb_tx = rte_eth_tx_burst(port_id, 0, tx_mbufs, nb_tx_burst);
        if (unlikely(nb_tx < nb_tx_burst)) {
            for (uint16_t k = nb_tx; k < nb_tx_burst; k++) {
                rte_pktmbuf_free(tx_mbufs[k]);
            }
        }

        total_bytes_sent += nb_tx * PAGE_SIZE * sizeof(uint32_t);
        total_bytes_with_headers_received +=
            nb_tx * (sizeof(struct rte_ether_hdr)
                + sizeof(struct rte_ipv4_hdr)
                + sizeof(struct rte_udp_hdr)
                + PAGE_SIZE * sizeof(uint32_t));

        end_tsc = rte_rdtsc();
        total_tsc += end_tsc - start_tsc;
    }

    double seconds = (double) total_tsc / rte_get_tsc_hz();
    double throughput = (total_bytes_sent * 8) / (seconds * 1000000000);
    double throughput_with_headers =
        (total_bytes_with_headers_received * 8) / (seconds * 1000000000);

    printf("Transmission complete.\n");
    printf("Total time: %.2f seconds\n", seconds);
    printf("Total data sent: %lu bytes\n", total_bytes_sent);
    printf("Throughput: %.2f Gbps\n", throughput);
    printf("Throughput with headers: %.2f Gbps\n", throughput_with_headers);

    return 0;
}