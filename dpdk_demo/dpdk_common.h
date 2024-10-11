#pragma once

#include <rte_ether.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_ethdev.h>

#define NUM_ITERATIONS 10000

#define PAGE_SIZE 500
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

#define SRC_IP "172.31.32.0"
#define DST_IP "172.31.40.91"

#define SRC_PORT 12345
#define DST_PORT 23456

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .max_lro_pkt_size = RTE_ETHER_MAX_LEN,
    },
};

static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = 1;
    int retval;
    uint16_t q;

    if (port >= rte_eth_dev_count_avail()) {
        fprintf(stderr, "Port %u is not available\n", port);
        return -1;
    }

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0) {
        fprintf(stderr, "Cannot configure port %u: %s\n", port, rte_strerror(-retval));
        return retval;
    }

    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
            rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0) {
            fprintf(stderr, "Cannot setup RX queue for port %u: %s\n", port, rte_strerror(-retval));
            return retval;
        }
    }

    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
            rte_eth_dev_socket_id(port), NULL);
        if (retval < 0) {
            fprintf(stderr, "Cannot setup TX queue for port %u: %s\n", port, rte_strerror(-retval));
            return retval;
        }
    }

    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        fprintf(stderr, "Cannot start port %u: %s\n", port, rte_strerror(-retval));
        return retval;
    }

    struct rte_ether_addr addr;
    rte_eth_macaddr_get(port, &addr);
    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
           port,
           addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
           addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);

    return 0;
}