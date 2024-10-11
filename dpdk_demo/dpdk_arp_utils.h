#include "dpdk_common.h"
#include <rte_arp.h>
#include <rte_cycles.h>

static int get_default_gateway_ip(uint32_t *gateway_ip) {
    FILE *fp;
    char line[100], *p, *c;

    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        perror("Failed to open /proc/net/route");
        return -1;
    }

    while(fgets(line, 100, fp)) {
        p = strtok(line, "\t");
        c = strtok(NULL, "\t");

        if (p != NULL && c != NULL) {
            if (strcmp(c, "00000000") == 0) {
                c = strtok(NULL, "\t");
                if (c) {
                    *gateway_ip = strtoul(c, NULL, 16);
                    fclose(fp);
                    return 0;
                }
            }
        }
    }

    fclose(fp);
    return -1;
}

static int send_arp_request(uint16_t port_id, struct rte_mempool *mbuf_pool,
                            uint32_t src_ip, uint32_t dst_ip, struct rte_ether_addr *dst_mac) {
    struct rte_mbuf *arp_mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (arp_mbuf == NULL) {
        printf("Failed to allocate mbuf for ARP request\n");
        return -1;
    }

    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(arp_mbuf, struct rte_ether_hdr *);
    struct rte_arp_hdr *arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);

    struct rte_ether_addr src_mac;
    rte_eth_macaddr_get(port_id, &src_mac);
    rte_ether_addr_copy(&src_mac, &eth_hdr->src_addr);
    memset(&eth_hdr->dst_addr, 0xff, RTE_ETHER_ADDR_LEN);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

    arp_hdr->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp_hdr->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp_hdr->arp_plen = sizeof(uint32_t);
    arp_hdr->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    rte_ether_addr_copy(&src_mac, &arp_hdr->arp_data.arp_sha);
    arp_hdr->arp_data.arp_sip = src_ip;
    memset(&arp_hdr->arp_data.arp_tha, 0, RTE_ETHER_ADDR_LEN);
    arp_hdr->arp_data.arp_tip = dst_ip;

    arp_mbuf->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
    arp_mbuf->pkt_len = arp_mbuf->data_len;

    const uint16_t nb_tx = rte_eth_tx_burst(port_id, 0, &arp_mbuf, 1);
    if (unlikely(nb_tx == 0)) {
        rte_pktmbuf_free(arp_mbuf);
        printf("Failed to send ARP request\n");
        return -1;
    }

    struct rte_mbuf *rx_mbufs[BURST_SIZE];
    uint16_t nb_rx;
    int max_retries = 5;
    while (max_retries--) {
        nb_rx = rte_eth_rx_burst(port_id, 0, rx_mbufs, BURST_SIZE);
        for (int i = 0; i < nb_rx; i++) {
            struct rte_ether_hdr *rx_eth_hdr = rte_pktmbuf_mtod(rx_mbufs[i], struct rte_ether_hdr *);
            if (rx_eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP)) {
                struct rte_arp_hdr *rx_arp_hdr = (struct rte_arp_hdr *)(rx_eth_hdr + 1);
                if (rx_arp_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY) &&
                    rx_arp_hdr->arp_data.arp_sip == dst_ip) {
                    rte_ether_addr_copy(&rx_arp_hdr->arp_data.arp_sha, dst_mac);
                    rte_pktmbuf_free(rx_mbufs[i]);
                    return 0;
                }
            }
            rte_pktmbuf_free(rx_mbufs[i]);
        }
        rte_delay_ms(100);
    }

    printf("ARP reply not received\n");
    return -1;
}
