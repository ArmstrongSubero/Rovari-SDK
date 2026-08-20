/*
 * Copyright 2026 Rovari - RISC-V Embedded Systems
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet driver for CH32V307 internal 10BASE-T PHY + lwIP.
 * Bare metal polling mode (NO_SYS=1).
 *
 * Requires HSE (8MHz crystal) for PLL3 Ethernet clock.
 * HSI clock options will NOT work with Ethernet.
 */

#ifndef ROVARI_ETH_H
#define ROVARI_ETH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PHY address for internal 10BASE-T PHY */
#define ROVARI_PHY_ADDRESS  1

/* DMA ring sizes */
#define ETH_RX_RING_SIZE    6
#define ETH_TX_RING_SIZE    2

/* Maximum Ethernet payload (MTU) */
#define MAX_ETH_PAYLOAD     1500

/*
 * Initialize Ethernet hardware + lwIP stack.
 * Configures PLL3, MAC, PHY, DMA descriptors, and lwIP.
 * Call once from app_init().
 * Returns 0 on success, non-zero on error (PHY reset timeout, etc).
 */
uint8_t eth_init_stack(void);

/*
 * Poll for incoming packets and process lwIP timeouts.
 * Call from app_run() on every iteration.
 * This is the bare-metal main loop hook.
 */
void eth_poll(void);

/*
 * Configure static IP address.
 * Call after eth_init_stack() if not using DHCP.
 * ip, netmask, gateway are IPv4 dotted-quad strings, e.g. "192.168.1.10".
 */
void eth_set_ip(const char *ip, const char *netmask, const char *gateway);

/*
 * Start DHCP client.
 * Call after eth_init_stack() to get an IP address automatically.
 */
void eth_dhcp_start(void);

/*
 * Check if Ethernet link is up.
 * Returns 1 if PHY reports link, 0 otherwise.
 */
uint8_t eth_is_link_up(void);

/*
 * Get the assigned IP address as a string.
 * Returns pointer to a static buffer, e.g. "192.168.1.10".
 * Returns "0.0.0.0" if no address assigned yet.
 */
const char *eth_get_ip(void);

/*
 * Get the MAC address.
 * Fills mac[6] with the hardware address derived from chip UID.
 */
void eth_get_mac(uint8_t *mac);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Eth {
public:
    Eth() {}

    uint8_t begin(void) { return eth_init_stack(); }
    void poll(void) { eth_poll(); }

    void setStaticIP(const char *ip, const char *netmask, const char *gw) {
        eth_set_ip(ip, netmask, gw);
    }
    void startDHCP(void) { eth_dhcp_start(); }

    uint8_t linkUp(void) { return eth_is_link_up(); }
    const char *ip(void) { return eth_get_ip(); }

    void mac(uint8_t *out) { eth_get_mac(out); }
};

#endif /* __cplusplus */

#endif /* ROVARI_ETH_H */
