/*
 * Copyright 2026 Rovari - RISC-V Embedded Systems
 * Copyright 2023 Xerbo (original ch32-lwip eth driver, Apache 2.0)
 * Copyright 2021 Nanjing Qinheng Microelectronics Co., Ltd. (WCH EVT, Apache 2.0)
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rovari_eth.c
 * @brief Ethernet driver: CH32V307 10BASE-T MAC/PHY + lwIP.
 *
 * @sevs-callbacks  Assigns function-pointer callbacks to the lwIP netif
 *                  (linkoutput/output/init); JPL Rule 9 suppressed per
 *                  SEVS Section 2.10. lwIP and the WCH ETH HAL are vendor
 *                  code consumed as a boundary (SEVS Section 1.5).
 *
 * Based on Xerbo's ch32-lwip project, adapted for the Rovari SDK.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "ch32v30x_eth.h"
#include "rovari_eth.h"
#include <lwip/init.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>
#include <lwip/etharp.h>
#include <lwip/dhcp.h>
#include <lwip/ip4_addr.h>
#include <netif/ethernet.h>

/* Bounded poll cap for PLL3 ready. */
#define ETH_PLL3_TIMEOUT 1000000U

/* From ch32v30x_eth.c (vendor HAL globals) */
extern ETH_DMADESCTypeDef *DMATxDescToSet;
extern ETH_DMADESCTypeDef *DMARxDescToGet;

/* delay_us from rovari SDK */
extern void Delay_Us(uint32_t us);

/* DMA descriptors and buffers (4-byte aligned) */
__attribute__((aligned(4))) static ETH_DMADESCTypeDef eth_dma_rx[ETH_RX_RING_SIZE];
__attribute__((aligned(4))) static ETH_DMADESCTypeDef eth_dma_tx[ETH_TX_RING_SIZE];
__attribute__((aligned(4))) static uint8_t eth_buf_rx[ETH_RX_RING_SIZE][ETH_MAX_PACKET_SIZE];
__attribute__((aligned(4))) static uint8_t eth_buf_tx[ETH_TX_RING_SIZE][ETH_MAX_PACKET_SIZE];

/* lwIP netif (single interface) */
static struct netif rovari_netif;

/* RX state (set in ISR, consumed in eth_poll) */
static volatile uint8_t rx_frame_ready = 0;
static struct pbuf *rx_pbuf = NULL;

/* Link state (set in ISR, consumed in eth_poll) */
static volatile uint8_t link_changed = 0;

/* IP address string buffer */
static char ip_str[16];

/* Low-level packet I/O */

/**
 * @brief Copy a frame into the current TX descriptor and hand it to the DMA.
 * @param[in] buffer Frame bytes.
 * @param[in] len    Frame length.
 * @return ETH_SUCCESS, or ETH_ERROR if the TX descriptor is busy.
 * @req REQ-ROVARI-ETH-0011
 * @req REQ-ROVARI-ETH-0021
 */
static uint32_t eth_send_packet(const uint8_t *buffer, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buffer);
    if (DMATxDescToSet->Status & ETH_DMATxDesc_OWN)
        return ETH_ERROR;

    memcpy((uint8_t *)ETH_GetCurrentTxBufferAddress(), buffer, len);

    DMATxDescToSet->ControlBufferSize = len & ETH_DMATxDesc_TBS1;
    DMATxDescToSet->Status |= ETH_DMATxDesc_LS | ETH_DMATxDesc_FS;
    DMATxDescToSet->Status |= ETH_DMATxDesc_OWN;
    DMATxDescToSet->Status |= ETH_DMATxDesc_CIC_TCPUDPICMP_Full;

    if (ETH->DMASR & ETH_DMASR_TBUS) {
        ETH->DMASR = ETH_DMASR_TBUS;
        ETH->DMATPDR = 0;
    }

    DMATxDescToSet = (ETH_DMADESCTypeDef *)DMATxDescToSet->Buffer2NextDescAddr;
    return ETH_SUCCESS;
}

/**
 * @brief Fetch the next received frame from the RX descriptor ring.
 * @param[out] buffer Receives a pointer to the frame data.
 * @param[out] len    Receives the frame length.
 * @return ETH_SUCCESS, or ETH_ERROR if no complete frame is ready.
 * @req REQ-ROVARI-ETH-0011
 * @req REQ-ROVARI-ETH-0021
 */
static uint32_t eth_get_packet(uint8_t **buffer, uint16_t *len)
{
    SEVS_REQUIRE_NOT_NULL(buffer);
    SEVS_REQUIRE_NOT_NULL(len);
    if (DMARxDescToGet->Status & ETH_DMARxDesc_OWN) {
        if (ETH->DMASR & ETH_DMASR_RBUS) {
            ETH->DMASR = ETH_DMASR_RBUS;
            ETH->DMARPDR = 0;
        }
        return ETH_ERROR;
    }

    uint32_t status = DMARxDescToGet->Status;
    if ((status & ETH_DMARxDesc_LS) && (status & ETH_DMARxDesc_FS) && !(status & ETH_DMARxDesc_ES)) {
        *len = ((status & ETH_DMARxDesc_FL) >> 16) - 4;
        *buffer = (uint8_t *)DMARxDescToGet->Buffer1Addr;
    } else {
        DMARxDescToGet->Status |= ETH_DMARxDesc_OWN;
        DMARxDescToGet = (ETH_DMADESCTypeDef *)DMARxDescToGet->Buffer2NextDescAddr;
        return ETH_ERROR;
    }

    DMARxDescToGet->Status |= ETH_DMARxDesc_OWN;
    DMARxDescToGet = (ETH_DMADESCTypeDef *)DMARxDescToGet->Buffer2NextDescAddr;
    return ETH_SUCCESS;
}

/* lwIP netif callbacks */

/**
 * @brief lwIP link-output callback: transmit a pbuf.
 * @param[in] netif The network interface (unused).
 * @param[in] p     The pbuf to transmit.
 * @return ERR_OK on success, ERR_IF on TX failure.
 * @req REQ-ROVARI-ETH-0011
 */
static err_t rovari_netif_output(struct netif *netif, struct pbuf *p)
{
    SEVS_REQUIRE_NOT_NULL(p);
    (void)netif;
    LINK_STATS_INC(link.xmit);

    if (eth_send_packet(p->payload, p->tot_len) == ETH_ERROR)
        return ERR_IF;

    return ERR_OK;
}

/**
 * @brief lwIP netif init callback: set up outputs, MTU, flags, and MAC.
 * @param[in,out] netif The network interface to initialize.
 * @return ERR_OK.
 * @req REQ-ROVARI-ETH-0010
 */
static err_t rovari_netif_init(struct netif *netif)
{
    SEVS_REQUIRE_NOT_NULL(netif);
    netif->linkoutput = rovari_netif_output;
    netif->output     = etharp_output;
    netif->mtu        = MAX_ETH_PAYLOAD;
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->hostname   = "rovari";
    netif->name[0]    = 'r';
    netif->name[1]    = 'v';

    eth_get_mac(netif->hwaddr);
    ETH_MACAddressConfig(ETH_MAC_Address0, netif->hwaddr);
    netif->hwaddr_len = 6;

    return ERR_OK;
}

/* PHY / MAC init */

/**
 * @brief Configure PLL3 to produce the 60 MHz Ethernet MAC clock.
 * @req REQ-ROVARI-ETH-0010
 * @req REQ-ROVARI-ETH-0020
 */
static void eth_configure_clock(void)
{
    /* PLL3: HSE 8MHz / PREDIV2(2) = 4MHz * PLL3(15) = 60MHz for ETH MAC */
    RCC_PREDIV2Config(RCC_PREDIV2_Div2);
    RCC_PLL3Config(RCC_PLL3Mul_15);
    RCC_PLL3Cmd(ENABLE);
    for (uint32_t i = 0U; i < ETH_PLL3_TIMEOUT; i++) {
        if (RCC_GetFlagStatus(RCC_FLAG_PLL3RDY) != 0) {
            break;
        }
    }
}

/**
 * @brief Initialize the MAC and PHY with bounded reset polling.
 * @return 0 on success, 1 on MAC reset timeout, 2 on PHY reset timeout.
 * @req REQ-ROVARI-ETH-0010
 * @req REQ-ROVARI-ETH-0020
 */
static uint32_t eth_hw_init(void)
{
    /* Enable MAC clocks */
    RCC_AHBPeriphClockCmd(
        RCC_AHBPeriph_ETH_MAC | RCC_AHBPeriph_ETH_MAC_Tx | RCC_AHBPeriph_ETH_MAC_Rx,
        ENABLE);

    /* Enable internal 10BASE-T PHY */
    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;

    /* Reset MAC */
    ETH_DeInit();
    ETH_SoftwareReset();
    for (uint8_t i = 0; i < 100; i++) {
        Delay_Us(10000);
        if ((ETH->DMABMR & ETH_DMABMR_SR) == 0)
            break;
        if (i == 99)
            return 1; /* MAC reset timeout */
    }

    /* Configure MAC */
    ETH_InitTypeDef eth;
    ETH_StructInit(&eth);
    eth.ETH_BroadcastFramesReception = ETH_BroadcastFramesReception_Enable;
    eth.ETH_AutoNegotiation = ETH_AutoNegotiation_Enable;
    eth.ETH_DropTCPIPChecksumErrorFrame = ETH_DropTCPIPChecksumErrorFrame_Enable;
    eth.ETH_ChecksumOffload = ETH_ChecksumOffload_Enable;
    eth.ETH_AutomaticPadCRCStrip = ETH_AutomaticPadCRCStrip_Enable;

    /* Set MDIO clock divider */
    ETH->MACMIIAR &= MACMIIAR_CR_MASK;

    /* Reset PHY */
    ETH_WritePHYRegister(ROVARI_PHY_ADDRESS, PHY_BMCR, PHY_Reset);
    for (uint8_t i = 0; i < 100; i++) {
        Delay_Us(10000);
        if ((ETH_ReadPHYRegister(ROVARI_PHY_ADDRESS, PHY_BMCR) & PHY_Reset) == 0)
            break;
        if (i == 99)
            return 2; /* PHY reset timeout */
    }

    /* Apply MAC register settings */
    ETH->MACHTHR = eth.ETH_HashTableHigh;
    ETH->MACHTLR = eth.ETH_HashTableLow;

    ETH->MACCR &= MACCR_CLEAR_MASK;
    ETH->MACCR |= (eth.ETH_Watchdog | eth.ETH_Jabber | eth.ETH_InterFrameGap |
                    eth.ETH_CarrierSense | eth.ETH_Speed | eth.ETH_LoopbackMode |
                    eth.ETH_Mode | eth.ETH_ChecksumOffload | eth.ETH_AutomaticPadCRCStrip |
                    eth.ETH_RetryTransmission | eth.ETH_BackOffLimit |
                    eth.ETH_DeferralCheck | ETH_Internal_Pull_Up_Res_Enable);

    ETH->MACFFR = (eth.ETH_ReceiveAll | eth.ETH_SourceAddrFilter |
                   eth.ETH_PassControlFrames | eth.ETH_BroadcastFramesReception |
                   eth.ETH_DestinationAddrFilter | eth.ETH_PromiscuousMode |
                   eth.ETH_MulticastFramesFilter | eth.ETH_UnicastFramesFilter);

    ETH->MACFCR &= MACFCR_CLEAR_MASK;
    ETH->MACFCR |= ((eth.ETH_PauseTime << 16) | eth.ETH_ZeroQuantaPause |
                     eth.ETH_PauseLowThreshold | eth.ETH_UnicastPauseFrameDetect |
                     eth.ETH_ReceiveFlowControl | eth.ETH_TransmitFlowControl);

    ETH->MACVLANTR = (eth.ETH_VLANTagComparison | eth.ETH_VLANTagIdentifier);

    ETH->DMAOMR &= DMAOMR_CLEAR_MASK;
    ETH->DMAOMR |= (eth.ETH_DropTCPIPChecksumErrorFrame | eth.ETH_ReceiveStoreForward |
                     eth.ETH_FlushReceivedFrame | eth.ETH_TransmitStoreForward |
                     eth.ETH_TransmitThresholdControl | eth.ETH_ForwardErrorFrames |
                     eth.ETH_ForwardUndersizedGoodFrames | eth.ETH_ReceiveThresholdControl |
                     eth.ETH_SecondFrameOperate);

    ETH->DMABMR = (eth.ETH_AddressAlignedBeats | eth.ETH_FixedBurst |
                   eth.ETH_RxDMABurstLength | eth.ETH_TxDMABurstLength |
                   (eth.ETH_DescriptorSkipLength << 2) | eth.ETH_DMAArbitration |
                   ETH_DMABMR_USP);

    /* Enable DMA interrupts: RX complete + link status change */
    ETH_DMAITConfig(ETH_DMA_IT_NIS | ETH_DMA_IT_R | ETH_DMA_IT_PHYLINK, ENABLE);
    NVIC_EnableIRQ(ETH_IRQn);

    return 0;
}

/* IRQ handler */

/**
 * @brief Ethernet ISR: handle RX-complete and PHY-link interrupts.
 * @req REQ-ROVARI-ETH-0014
 */
void __attribute__((interrupt("machine"))) ETH_IRQHandler(void)
{
    /* RX complete */
    if (ETH_GetDMAITStatus(ETH_DMA_IT_R)) {
        ETH_DMAClearITPendingBit(ETH_DMA_IT_R);
        if (rx_frame_ready == 0) {
            uint8_t *buf;
            uint16_t len;
            if (eth_get_packet(&buf, &len) == ETH_SUCCESS) {
                rx_pbuf = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
                if (rx_pbuf != NULL) {
                    pbuf_take(rx_pbuf, buf, len);
                    rx_frame_ready = 1;
                }
            }
        }
    }

    /* PHY link status change */
    if (ETH_GetDMAITStatus(ETH_DMA_IT_PHYLINK)) {
        ETH_DMAClearITPendingBit(ETH_DMA_IT_PHYLINK);
        link_changed = 1;
    }

    ETH_DMAClearITPendingBit(ETH_DMA_IT_NIS);
}

/* Public API */

/**
 * @brief Derive a MAC address from the device unique ID.
 * @param[out] mac Receives the 6-byte MAC address.
 * @req REQ-ROVARI-ETH-0013
 * @req REQ-ROVARI-ETH-0021
 */
void eth_get_mac(uint8_t *mac)
{
    SEVS_REQUIRE_NOT_NULL(mac);
    const uint8_t *uid = (uint8_t *)0x1FFFF7E8;
    mac[0] = uid[5];
    mac[1] = uid[4];
    mac[2] = uid[3];
    mac[3] = uid[2];
    mac[4] = uid[1];
    mac[5] = uid[0];
}

/**
 * @brief Bring up the MAC/PHY, DMA chains, and the lwIP netif.
 * @return 0 on success, non-zero on MAC/PHY init failure.
 * @req REQ-ROVARI-ETH-0010
 */
uint8_t eth_init_stack(void)
{
    uint32_t err;

    eth_configure_clock();

    err = eth_hw_init();
    if (err != 0)
        return (uint8_t)err;

    /* Init DMA descriptor chains */
    ETH_DMARxDescChainInit(eth_dma_rx, &eth_buf_rx[0][0], ETH_RX_RING_SIZE);
    ETH_DMATxDescChainInit(eth_dma_tx, &eth_buf_tx[0][0], ETH_TX_RING_SIZE);

    /* Start MAC */
    ETH_Start();

    /* Init lwIP */
    lwip_init();

    /* Add network interface with default zero IP (set later by user or DHCP) */
    ip_addr_t zero = IPADDR4_INIT(0);
    netif_add(&rovari_netif, &zero, &zero, &zero, NULL, &rovari_netif_init, &ethernet_input);
    netif_set_default(&rovari_netif);
    netif_set_up(&rovari_netif);

    return 0;
}

/**
 * @brief Service link changes, deliver RX frames to lwIP, run lwIP timers.
 * @req REQ-ROVARI-ETH-0011
 */
void eth_poll(void)
{
    /* Handle link status changes */
    if (link_changed) {
        link_changed = 0;
        if (ETH_ReadPHYRegister(ROVARI_PHY_ADDRESS, PHY_BMSR) & PHY_Linked_Status) {
            /* Read duplex mode from auto-negotiation result */
            uint32_t mode;
            if (ETH_ReadPHYRegister(ROVARI_PHY_ADDRESS, PHY_BMCR) & (1 << 8))
                mode = ETH_Mode_FullDuplex;
            else
                mode = ETH_Mode_HalfDuplex;

            /* Apply negotiated settings to MAC */
            ETH->MACCR &= ~0x0000C800;
            ETH->MACCR |= mode | ETH_Speed_10M;
            netif_set_link_up(&rovari_netif);
        } else {
            netif_set_link_down(&rovari_netif);
        }
    }

    /* Feed received frames to lwIP */
    if (rx_frame_ready) {
        LINK_STATS_INC(link.recv);
        if (rovari_netif.input(rx_pbuf, &rovari_netif) != ERR_OK)
            pbuf_free(rx_pbuf);
        rx_frame_ready = 0;
    }

    /* Process lwIP timers (ARP, DHCP, TCP retransmit, etc.) */
    sys_check_timeouts();
}

/**
 * @brief Set a static IP address, netmask, and gateway.
 * @param[in] ip      Dotted-quad IP string.
 * @param[in] netmask Dotted-quad netmask string.
 * @param[in] gateway Dotted-quad gateway string.
 * @req REQ-ROVARI-ETH-0012
 * @req REQ-ROVARI-ETH-0021
 */
void eth_set_ip(const char *ip, const char *netmask, const char *gateway)
{
    SEVS_REQUIRE_NOT_NULL(ip);
    SEVS_REQUIRE_NOT_NULL(netmask);
    SEVS_REQUIRE_NOT_NULL(gateway);
    ip_addr_t addr, mask, gw;
    ip4addr_aton(ip, ip_2_ip4(&addr));
    ip4addr_aton(netmask, ip_2_ip4(&mask));
    ip4addr_aton(gateway, ip_2_ip4(&gw));
    netif_set_addr(&rovari_netif, ip_2_ip4(&addr), ip_2_ip4(&mask), ip_2_ip4(&gw));
}

/**
 * @brief Start DHCP on the interface.
 * @req REQ-ROVARI-ETH-0012
 */
void eth_dhcp_start(void)
{
    dhcp_start(&rovari_netif);
}

/**
 * @brief Report whether the PHY link is up.
 * @return 1 if linked, 0 otherwise.
 * @req REQ-ROVARI-ETH-0013
 */
uint8_t eth_is_link_up(void)
{
    return (ETH_ReadPHYRegister(ROVARI_PHY_ADDRESS, PHY_BMSR) & PHY_Linked_Status) ? 1 : 0;
}

/**
 * @brief Get the current IPv4 address as a string.
 * @return Pointer to a static dotted-quad string buffer.
 * @req REQ-ROVARI-ETH-0013
 */
const char *eth_get_ip(void)
{
    ip4addr_ntoa_r(netif_ip4_addr(&rovari_netif), ip_str, sizeof(ip_str));
    return ip_str;
}
