#pragma codeseg BANK3
#pragma constseg BANK3

#include "rtl837x_common.h"
#include "rtl837x_lldp.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_common.h"
#include "dhcp.h"
#include "uip.h"
#include "uip/uip.h"
#include "machine.h"
#include "version.h"
#include "timer.h"
#include "rtl837x_regs.h"
#include "rtl837x_port.h"
#include "phy.h"
#include "rtl837x_trap.h"
#include "peer.h"

extern __xdata bool lldp_enabled;
extern __xdata uint8_t sfr_data[4];
extern __code const struct machine machine;
extern __xdata uint8_t sfr_data[4];
static __code const uint8_t lldp_oui_8021[3] = {0x00, 0x80, 0xc2};
static __code const uint8_t lldp_oui_8023[3] = {0x00, 0x12, 0x0f};

static __xdata struct timer lldp_timer;
__xdata uint8_t lldp_seconds;
__xdata uint8_t lldp_fast;
__xdata bool lldp_was_enabled;
__xdata uint16_t lldp_links_last;
__xdata uint16_t lldp_logical_port_status = 0xffff;

/* Announced as our management address instead of our own (`lldp addr`):
 * the High chip of a REM-8x has no IP of its own, Low sets its address */
__xdata uint8_t lldp_mgmt_ip[4];

__xdata uint16_t lldp_nb_ttl[LLDP_NB_PORTS];
__xdata char lldp_nb_sys[LLDP_NB_PORTS][LLDP_NB_SYS_LEN + 1];
__xdata char lldp_nb_port[LLDP_NB_PORTS][LLDP_NB_PORT_LEN + 1];
__xdata uint8_t lldp_nb_mac[LLDP_NB_PORTS][6];
__xdata uint8_t lldp_nb_ip[LLDP_NB_PORTS][4];

static void lldp_nb_clear(void)
{
    for (uint8_t i = 0; i < LLDP_NB_PORTS; i++)
        lldp_nb_ttl[i] = 0;
}

void lldp_init(void) __banked
{
    timer_set(&lldp_timer, SYS_TICK_HZ);
    lldp_seconds = 0;
    lldp_fast = 0;
    lldp_was_enabled = false;
    lldp_links_last = 0;
    lldp_nb_clear();
    for (uint8_t i = 0; i < 4; i++)
        lldp_mgmt_ip[i] = 0;
}

static uint16_t lldp_links(void) __reentrant
{
    reg_read_m(RTL837X_REG_LINKS_STS);
    return ((uint16_t)sfr_data[2] << 8) | sfr_data[1];
}

void lldp_tick(void) __banked
{
    static __xdata uint16_t links;	// Internal RAM is full

    if (!timer_expired(&lldp_timer))
        return;

    timer_reset(&lldp_timer);

    if (!lldp_enabled) {
        if (lldp_was_enabled)
            lldp_nb_clear();
        lldp_was_enabled = false;
        return;
    }

    links = lldp_links();
    // Neighbors age out with their TTL, and go with the link
    for (uint8_t i = 0; i < LLDP_NB_PORTS; i++) {
        if (!lldp_nb_ttl[i])
            continue;
        if (!(links & ((uint16_t)1 << i)))
            lldp_nb_ttl[i] = 0;
        else
            lldp_nb_ttl[i]--;
    }
    if (!lldp_was_enabled || (links & ~lldp_links_last & lldp_logical_port_status))
        lldp_fast = LLDP_FAST_TX_COUNT;
    lldp_links_last = links;
    lldp_was_enabled = true;

    if (lldp_fast) {
        lldp_fast--;
        lldp_seconds = 0;
        peer_lldp_addr();
        lldp_send();
        return;
    }

    if (++lldp_seconds < LLDP_TX_INTERVAL_S)
        return;
    lldp_seconds = 0;
    peer_lldp_addr();	// Also after a reboot of the peer: it forgets
    lldp_send();
}

//Outgoing LLDP packet.

struct lldp_pkt {
    struct uip_eth_addr dst;
    struct uip_eth_addr src;
    struct rtl_tag rtl_tag;
    uint16_t ether_type;

    uint8_t payload[LLDP_MAX_PAYLOAD];
};

#define LLDP_O ((__xdata struct lldp_pkt *)&uip_buf[RTL_FRAME_DESC_SIZE])

// The type is 7 bits and the length is 9 bits
// we can represent this as the 7 most significant
// bits of the type bit and leave the least significant
// bit as 0 always as we're not going to need 2^9 for
// the length at any point
#define LLDP_TYPE(value) (value << 1)

static void lldp_send_ports(uint16_t ttl, uint16_t ports) __reentrant
{
    __xdata uint8_t *p;
    __xdata uint8_t *tail;
    uint8_t port_position;

    lldp_set_addresses();
    lldp_set_rtl_wrapper();

    p = LLDP_O->payload;

    //Chassis ID
    *p++ = LLDP_TYPE(LLDP_CHASSIS_ID_TLV_TYPE);
    *p++ = LLDP_CHASSIS_ID_TLV_LENGTH;
    *p++ = LLDP_CHASSIS_ID_TLV_SUBTYPE;

    memcpy(p, uip_ethaddr.addr, MAC_ADDR_LEN);
    p += MAC_ADDR_LEN;

    //Port ID
    *p++ = LLDP_TYPE(LLDP_PORT_ID_TLV_TYPE);
    *p++ = LLDP_PORT_ID_TLV_LENGTH;
    *p++ = LLDP_PORT_ID_TLV_SUBTYPE;
    memcpyc(p, LLDP_PORT_ID_PREFIX, LLDP_PORT_ID_PREFIX_LENGTH);
    p += LLDP_PORT_ID_PREFIX_LENGTH;
    port_position = (p-LLDP_O->payload);
	*p++ = '0';       // filled in per port below
#if MACHINE_PHYS_PORTS > 9
    *p++ = '0';
#endif

    //TTL
    *p++ = LLDP_TYPE(LLDP_TTL_TLV_TYPE);
    *p++ = LLDP_TTL_TLV_LENGTH;
    *p++ = ttl >> 8;
    *p++ = ttl & 0xff;

    p += lldp_sysname(p);
    p += lldp_sysdesc(p);

    //System Capabilities: supported, enabled
    *p++ = LLDP_TYPE(LLDP_CAPABILITIES_TLV_TYPE);
    *p++ = LLDP_CAPABILITIES_TLV_LENGTH;
    *p++ = LLDP_CAPABILITY_BRIDGE >> 8;
    *p++ = LLDP_CAPABILITY_BRIDGE & 0xff;
    *p++ = LLDP_CAPABILITY_BRIDGE >> 8;
    *p++ = LLDP_CAPABILITY_BRIDGE & 0xff;

    //Management Address: IPv4
    if (lldp_mgmt_ip[0] || (uip_hostaddr[0] | uip_hostaddr[1])) {
        *p++ = LLDP_TYPE(LLDP_MGMT_ADDR_TLV_TYPE);
        *p++ = LLDP_MGMT_ADDR_TLV_LENGTH;
        *p++ = LLDP_MGMT_ADDR_STR_LENGTH;
        *p++ = LLDP_MGMT_ADDR_SUBTYPE_IPV4;
        if (lldp_mgmt_ip[0])
            memcpy(p, lldp_mgmt_ip, sizeof(uip_ipaddr_t));
        else
            memcpy(p, (__xdata uint8_t *)uip_hostaddr, sizeof(uip_ipaddr_t));
        p += sizeof(uip_ipaddr_t);
        *p++ = LLDP_MGMT_IF_NUMBERING_UNKNOWN;
        *p++ = 0; //interface number
        *p++ = 0;
        *p++ = 0;
        *p++ = 0;
        *p++ = 0; //OID length
    }

    tail = p;

    for (uint8_t port = MACHINE_MIN_PORT; port <= MACHINE_MAX_PORT; port++) {

        if ((1 << port) & ports) {
#if MACHINE_PHYS_PORTS > 9
            LLDP_O->payload[port_position] = '0' + machine.log_to_phys_port[port] / 10;
            LLDP_O->payload[port_position + 1] = '0' + machine.log_to_phys_port[port] % 10;
#else
            LLDP_O->payload[port_position] = '0' + machine.log_to_phys_port[port];
#endif
            LLDP_O->rtl_tag.pmask = HTONS((uint16_t)1 << port);

            p = tail;
            p += lldp_portdesc(p, port);
            p += lldp_port_org_tlvs(p, port);

            // End of LLDPDU TLV
            *p++ = 0x00; //padding
            *p++ = 0x00; //padding

            while (p < (LLDP_O->payload + LLDP_MIN_ETHERNET_PAYLOAD_LENGTH))
                *p++ = 0x00;

            /*
             * uip_len is the Ethernet frame length excluding FCS.
             *
             * The frame consists of:
             *
             *     dst mac
             *     src mac
             *     rtl_tag  sizeof(struct rtl_tag)
             *     EtherType (2 bytes)
             *     payload (how far we've moved p pointer)
             */
            uip_len = MAC_ADDR_LEN + MAC_ADDR_LEN + sizeof(struct rtl_tag) + LLDP_ETHERTYPE_LENGTH + (p - LLDP_O->payload);

            tcpip_output();
        }
    }
}

void lldp_send(void) __banked __reentrant
{
    lldp_send_ports(LLDP_TTL_TTL_SECONDS, lldp_logical_port_status);
}

void lldp_shutdown(uint16_t ports) __banked __reentrant
{
    lldp_send_ports(0, ports);
}

void lldp_set_addresses(void) __banked {
    // LLDP destination multicast addr: 01:80:c2:00:00:0e
    memcpyc(LLDP_O->dst.addr, "\x01\x80\xc2\x00\x00\x0e", MAC_ADDR_LEN);
    memcpy(LLDP_O->src.addr, uip_ethaddr.addr, MAC_ADDR_LEN);
}

void lldp_set_rtl_wrapper(void) __banked {
    /*
     * This is the RTL CPU tag, not the Ethernet EtherType.
     *     port 0 -> 0x0001
     *     port 1 -> 0x0002
     *     port 2 -> 0x0004
     */
    LLDP_O->rtl_tag.tag = HTONS(RTL_FRAME_TAG_ID);
    LLDP_O->rtl_tag.version = RTL_FRAME_TAG_VERSION;
    LLDP_O->rtl_tag.reason = 0x00;
    LLDP_O->rtl_tag.flags = HTONS(RTL_TAG_LEARN_DIS);

    LLDP_O->ether_type = HTONS(LLDP_ETHERTYPE);
}

uint8_t lldp_sysname(__xdata uint8_t *p) __banked
{
    uint8_t hostname_length = strlen_x(hostname);

    *p++ = LLDP_TYPE(LLDP_SYSNAME_TLV_TYPE);
    *p++ = hostname_length;
    memcpy(p, hostname, hostname_length);
    return hostname_length + 2;
}

uint8_t lldp_sysdesc(__xdata uint8_t *p) __banked
{
    uint8_t machine_name_length = strlen(machine.machine_name);
    uint8_t version_length = strlen(VERSION_SW);

    *p++ = LLDP_TYPE(LLDP_SYSDESC_TLV_TYPE);
    *p++ = machine_name_length + 1 + version_length;
    memcpyc(p, machine.machine_name, machine_name_length);
    p += machine_name_length;
    *p++ = ' ';
    memcpyc(p, VERSION_SW, version_length);
    return machine_name_length + 1 + version_length + 2;
}

uint8_t lldp_portdesc(__xdata uint8_t *p, uint8_t port) __banked
{
    uint8_t name_length = strlen_x(port_names[port]);

    if (!name_length)
        return 0;
    *p++ = LLDP_TYPE(LLDP_PORT_DESC_TLV_TYPE);
    *p++ = name_length;
    memcpy(p, port_names[port], name_length);
    return name_length + 2;
}

static __xdata uint8_t *lldp_org_header(__xdata uint8_t *p, __code const uint8_t *oui, uint8_t subtype, uint8_t length) __reentrant
{
    *p++ = (uint8_t)LLDP_TYPE(LLDP_ORG_TLV_TYPE);
    *p++ = LLDP_ORG_TLV_HEADER_LENGTH + length;
    memcpyc(p, oui, 3);
    p += 3;
    *p++ = subtype;
    return p;
}

static uint8_t lldp_link_code(uint8_t port) __reentrant
{
    uint8_t b;

    reg_read_m(port < 8 ? RTL837X_REG_LINKS : RTL837X_REG_LINKS_89);
    b = sfr_data[3 - ((port & 7) >> 1)];
    return (port & 1) ? b >> 4 : b & 0xf;
}

static __xdata uint8_t *lldp_mac_phy(__xdata uint8_t *p, uint8_t port) __reentrant
{
    uint16_t cap = 0;
    uint16_t mau = LLDP_MAU_UNKNOWN;
    uint8_t autoneg = 0;
    uint16_t v;

    p = lldp_org_header(p, lldp_oui_8023, LLDP_8023_SUBTYPE_MAC_PHY, 5);
    if (machine.is_sfp[port]) {
        if (lldp_link_code(port) == LINKS_CODE_1000M)
            mau = LLDP_MAU_1000BASE_X_FD;
    } else {
        autoneg = LLDP_AUTONEG_SUPPORTED;
        phy_read(port, PHY_MMD_AN, PHY_ANEG_CTRL);
        if (SFR_DATA_U16 & PHY_ANEG_CTRL_ENABLE)
            autoneg |= LLDP_AUTONEG_ENABLED;
        phy_read(port, PHY_MMD_AN, PHY_ANEG_ADV);
        v = SFR_DATA_U16;
        if (v & PHY_ANEG_ADV_10BASE_T_HD)
            cap |= LLDP_AUTONEG_CAP_10BASE_T;
        if (v & PHY_ANEG_ADV_10BASE_T_FD)
            cap |= LLDP_AUTONEG_CAP_10BASE_T_FD;
        if (v & PHY_ANEG_ADV_100BASE_TX_HD)
            cap |= LLDP_AUTONEG_CAP_100BASE_TX;
        if (v & PHY_ANEG_ADV_100BASE_TX_FD)
            cap |= LLDP_AUTONEG_CAP_100BASE_TX_FD;
        phy_read(port, PHY_MMD31, PHY_MMD31_GBCR);
        if (SFR_DATA_U16 & PHY_GBCR_1000BASE_T_FD)
            cap |= LLDP_AUTONEG_CAP_1000BASE_T_FD;
        phy_read(port, PHY_MMD31, PHY_MMD31_PHYSR);
        v = SFR_DATA_U16;
        switch (PHYSR_SPEED(v)) {
        case PHYSR_SPEED_10M:
            mau = (v & PHYSR_FULL_DUPLEX) ? LLDP_MAU_10BASE_T_FD : LLDP_MAU_10BASE_T_HD;
            break;
        case PHYSR_SPEED_100M:
            mau = (v & PHYSR_FULL_DUPLEX) ? LLDP_MAU_100BASE_TX_FD : LLDP_MAU_100BASE_TX_HD;
            break;
        case PHYSR_SPEED_1000M:
            mau = (v & PHYSR_FULL_DUPLEX) ? LLDP_MAU_1000BASE_T_FD : LLDP_MAU_1000BASE_T_HD;
            break;
        }
    }
    *p++ = autoneg;
    *p++ = cap >> 8;
    *p++ = cap & 0xff;
    *p++ = mau >> 8;
    *p++ = mau & 0xff;
    return p;
}

uint8_t lldp_port_org_tlvs(__xdata uint8_t *p, uint8_t port) __banked __reentrant
{
    __xdata uint8_t *start = p;
    uint16_t v;
    uint8_t lag;

    p = lldp_mac_phy(p, port);

    v = port_pvid_get(port);
    p = lldp_org_header(p, lldp_oui_8021, LLDP_8021_SUBTYPE_PVID, 2);
    *p++ = v >> 8;
    *p++ = v & 0xff;

    lag = port_lag_of(port);
    p = lldp_org_header(p, lldp_oui_8023, LLDP_8023_SUBTYPE_LINK_AGG, 5);
    *p++ = LLDP_AGG_CAPABLE | (lag == PORT_LAG_NONE ? 0 : LLDP_AGG_ACTIVE);
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = lag == PORT_LAG_NONE ? 0 : lag + 1;

    reg_read_m(RTL8373_REG_MAC_L2_PORT_MAX_LEN + ((uint16_t)port << 8));
    v = SFR_DATA_U16 & PORT_MAX_LEN_MASK;
    p = lldp_org_header(p, lldp_oui_8023, LLDP_8023_SUBTYPE_MAX_FRAME, 2);
    *p++ = v >> 8;
    *p++ = v & 0xff;

    return p - start;
}


/*
 * Incoming LLDPDUs. With LLDP on, the ASIC traps frames to 01:80:c2:00:00:0e
 * to the CPU (RMA 0E) instead of forwarding them; each one replaces what we
 * know about the neighbor on its port.
 */
struct lldp_in_hdr {
    struct uip_eth_addr dst;
    struct uip_eth_addr src;
    struct rtl_tag rtl_tag;
};

#define LLDP_H ((__xdata struct lldp_in_hdr *)&uip_buf[0])

/* Parser state in xdata: internal RAM has no room left, and pointers kept
 * here spare SDCC the temporaries it would otherwise put there */
__xdata uint8_t * __xdata lldp_rx_cur;	// The TLV being read
__xdata uint8_t * __xdata lldp_rx_end;
__xdata uint8_t * __xdata lldp_rx_src;	// Its value
__xdata char * __xdata lldp_rx_dst;
__xdata uint8_t * __xdata lldp_e_mac;	// The entry being filled
__xdata uint8_t * __xdata lldp_e_ip;
__xdata char * __xdata lldp_e_sys;
__xdata char * __xdata lldp_e_port;
__xdata uint16_t lldp_rx_len, lldp_rx_ttl;
__xdata uint8_t lldp_rx_port, lldp_rx_type, lldp_rx_k, lldp_rx_c, lldp_rx_max, lldp_rx_got;
__xdata char lldp_num[6];

/* Copy the value from byte skip on to lldp_rx_dst, cut to lldp_rx_max
 * characters; anything JSON or a terminal would not take becomes '.' */
static void lldp_str(uint8_t skip)
{
    lldp_rx_src += skip;
    lldp_rx_len -= skip;
    for (lldp_rx_k = 0; lldp_rx_len && lldp_rx_k < lldp_rx_max; lldp_rx_len--) {
        lldp_rx_c = *lldp_rx_src++;
        if (lldp_rx_c < 0x20 || lldp_rx_c > 0x7e || lldp_rx_c == '"' || lldp_rx_c == '\\')
            lldp_rx_c = '.';
        lldp_rx_dst[lldp_rx_k++] = lldp_rx_c;
    }
    lldp_rx_dst[lldp_rx_k] = 0;
}

static __code const char lldp_hex[] = "0123456789abcdef";

/* The MAC after the subtype byte as aa:bb:cc:dd:ee:ff to lldp_rx_dst */
static void lldp_mac_str(void)
{
    for (lldp_rx_k = 1; lldp_rx_k <= 6; lldp_rx_k++) {
        lldp_rx_c = lldp_rx_src[lldp_rx_k];
        *lldp_rx_dst++ = lldp_hex[lldp_rx_c >> 4];
        *lldp_rx_dst++ = lldp_hex[lldp_rx_c & 0xf];
        if (lldp_rx_k < 6)
            *lldp_rx_dst++ = ':';
    }
    *lldp_rx_dst = 0;
}

void lldp_in(void) __banked
{
    lldp_rx_k = TRAP_RX_BODY();
    lldp_rx_cur = &uip_buf[lldp_rx_k];
    lldp_rx_end = &uip_buf[uip_len < UIP_CONF_BUFFER_SIZE ? uip_len : UIP_CONF_BUFFER_SIZE];
    uip_len = 0;	// Nothing to send back
    lldp_rx_port = ((uint8_t)HTONS(LLDP_H->rtl_tag.pmask)) & 0x0f;
    if (!MACHINE_PORT_VALID(lldp_rx_port) || lldp_rx_cur + 2 > lldp_rx_end
        || lldp_rx_cur[0] != (LLDP_ETHERTYPE >> 8) || lldp_rx_cur[1] != (LLDP_ETHERTYPE & 0xff))
        return;
    lldp_rx_cur += 2;

    lldp_nb_ttl[lldp_rx_port] = 0;	// Invalid until the LLDPDU is complete
    lldp_e_sys = lldp_nb_sys[lldp_rx_port];
    lldp_e_port = lldp_nb_port[lldp_rx_port];
    lldp_e_mac = lldp_nb_mac[lldp_rx_port];
    lldp_e_ip = lldp_nb_ip[lldp_rx_port];
    lldp_e_sys[0] = 0;
    lldp_e_port[0] = 0;
    for (lldp_rx_k = 0; lldp_rx_k < 6; lldp_rx_k++)
        lldp_e_mac[lldp_rx_k] = 0;
    for (lldp_rx_k = 0; lldp_rx_k < 4; lldp_rx_k++)
        lldp_e_ip[lldp_rx_k] = 0;
    lldp_rx_got = 0;

    while (lldp_rx_cur + 2 <= lldp_rx_end) {
        lldp_rx_type = lldp_rx_cur[0] >> 1;
        lldp_rx_len = ((uint16_t)(lldp_rx_cur[0] & 1) << 8) | lldp_rx_cur[1];
        lldp_rx_src = lldp_rx_cur + 2;
        if (!lldp_rx_type)		// End of LLDPDU
            break;
        lldp_rx_cur = lldp_rx_src + lldp_rx_len;
        if (lldp_rx_cur > lldp_rx_end)
            return;			// Truncated
        if (!lldp_rx_len)
            continue;

        switch (lldp_rx_type) {
        case LLDP_CHASSIS_ID_TLV_TYPE:
            if (lldp_rx_src[0] == 4 && lldp_rx_len == 7) {		// MAC address
                for (lldp_rx_k = 0; lldp_rx_k < 6; lldp_rx_k++)
                    lldp_e_mac[lldp_rx_k] = lldp_rx_src[1 + lldp_rx_k];
            } else if (lldp_rx_src[0] != 5 && !lldp_e_sys[0]) {	// Not a network address
                lldp_rx_dst = lldp_e_sys;
                lldp_rx_max = LLDP_NB_SYS_LEN;
                lldp_str(1);
            }
            break;
        case LLDP_PORT_ID_TLV_TYPE:
            lldp_rx_dst = lldp_e_port;
            lldp_rx_max = LLDP_NB_PORT_LEN;
            if (lldp_rx_src[0] == 3 && lldp_rx_len == 7)		// MAC address
                lldp_mac_str();
            else if (lldp_rx_src[0] != 4)				// Not a network address
                lldp_str(1);
            break;
        case LLDP_TTL_TLV_TYPE:
            if (lldp_rx_len >= 2) {
                lldp_rx_ttl = ((uint16_t)lldp_rx_src[0] << 8) | lldp_rx_src[1];
                lldp_rx_got = 1;
            }
            break;
        case LLDP_PORT_DESC_TLV_TYPE:
            if (!lldp_e_port[0]) {
                lldp_rx_dst = lldp_e_port;
                lldp_rx_max = LLDP_NB_PORT_LEN;
                lldp_str(0);
            }
            break;
        case LLDP_SYSNAME_TLV_TYPE:			// Better than a chassis ID string
            lldp_rx_dst = lldp_e_sys;
            lldp_rx_max = LLDP_NB_SYS_LEN;
            lldp_str(0);
            break;
        case LLDP_MGMT_ADDR_TLV_TYPE:
            // Length of subtype + address, subtype, address
            if (lldp_rx_src[0] == 5 && lldp_rx_src[1] == LLDP_MGMT_ADDR_SUBTYPE_IPV4 && lldp_rx_len >= 6
                && !lldp_e_ip[0]) {
                for (lldp_rx_k = 0; lldp_rx_k < 4; lldp_rx_k++)
                    lldp_e_ip[lldp_rx_k] = lldp_rx_src[2 + lldp_rx_k];
            }
            break;
        }
    }
    // A TTL of 0 is a shutdown: the neighbor is gone
    if (lldp_rx_got)
        lldp_nb_ttl[lldp_rx_port] = lldp_rx_ttl;
}


static void lldp_print_u16(uint16_t v)
{
    lldp_rx_k = 5;
    lldp_num[5] = 0;
    do {
        lldp_num[--lldp_rx_k] = '0' + v % 10;
        v /= 10;
    } while (v);
    print_string_x(&lldp_num[lldp_rx_k]);
}

/* `lldp show`: the neighbors of this chip */
void lldp_show(void) __banked
{
    static __xdata uint8_t i, j, n;

    n = 0;
    print_string(lldp_enabled ? "LLDP on\n" : "LLDP off\n");
    for (i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
        if (!lldp_nb_ttl[i])
            continue;
        n++;
        print_string("port "); itoa(machine.log_to_phys_port[i]);
        print_string(": ");
        if (lldp_nb_sys[i][0]) {
            print_string_x(lldp_nb_sys[i]);
        } else {
            for (j = 0; j < 6; j++) {
                print_byte(lldp_nb_mac[i][j]);
                if (j < 5)
                    write_char(':');
            }
        }
        if (lldp_nb_port[i][0]) {
            print_string(" port ");
            print_string_x(lldp_nb_port[i]);
        }
        if (lldp_nb_ip[i][0]) {
            print_string(" ip ");
            for (j = 0; j < 4; j++) {
                itoa(lldp_nb_ip[i][j]);
                if (j < 3)
                    write_char('.');
            }
        }
        print_string(" ttl ");
        lldp_print_u16(lldp_nb_ttl[i]);
        write_char('\n');
    }
    if (!n)
        print_string("No neighbors\n");
}
