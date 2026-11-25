#ifndef _LLDP_H_
#define _LLDP_H_

#include <stdint.h>
#include "machine.h"	// MACHINE_PHYS_PORTS

#define LLDP_MIN_ETHERNET_PAYLOAD_LENGTH 46
#define LLDP_ETHERTYPE       0x88cc
#define LLDP_ETHERTYPE_LENGTH 2
#define LLDP_MAX_FRAME       256
#define LLDP_TX_INTERVAL_S   30
#define LLDP_HOLD_MULTIPLIER 4
#define LLDP_FAST_TX_COUNT   4

#define LLDP_PORT_ID_TLV_TYPE 2
#define LLDP_PORT_ID_PREFIX "Port "
#define LLDP_PORT_ID_PREFIX_LENGTH 5
/* One digit for the port number, two where the numbers go past 9 ("Port 05") */
#if MACHINE_PHYS_PORTS > 9
#define LLDP_PORT_ID_DIGITS 2
#else
#define LLDP_PORT_ID_DIGITS 1
#endif
#define LLDP_PORT_ID_TLV_LENGTH (1 + LLDP_PORT_ID_PREFIX_LENGTH + LLDP_PORT_ID_DIGITS)
#define LLDP_PORT_ID_TLV_SUBTYPE 0x05

#define LLDP_TTL_TLV_TYPE 3
#define LLDP_TTL_TLV_LENGTH 2
#define LLDP_TTL_TTL_SECONDS (LLDP_HOLD_MULTIPLIER * LLDP_TX_INTERVAL_S)

#define LLDP_CHASSIS_ID_TLV_TYPE 1
#define LLDP_CHASSIS_ID_TLV_LENGTH 7
#define LLDP_CHASSIS_ID_TLV_SUBTYPE 4

#define LLDP_PORT_DESC_TLV_TYPE 4
#define LLDP_ORG_TLV_TYPE 127
#define LLDP_ORG_TLV_HEADER_LENGTH 4
#define LLDP_8021_SUBTYPE_PVID 1
#define LLDP_8023_SUBTYPE_MAC_PHY 1
#define LLDP_8023_SUBTYPE_LINK_AGG 3
#define LLDP_AUTONEG_SUPPORTED 0x01
#define LLDP_AUTONEG_ENABLED 0x02
#define LLDP_AUTONEG_CAP_10BASE_T 0x4000
#define LLDP_AUTONEG_CAP_10BASE_T_FD 0x2000
#define LLDP_AUTONEG_CAP_100BASE_TX 0x0800
#define LLDP_AUTONEG_CAP_100BASE_TX_FD 0x0400
#define LLDP_AUTONEG_CAP_1000BASE_T_FD 0x0001
#define LLDP_MAU_UNKNOWN 0
#define LLDP_MAU_10BASE_T_HD 10
#define LLDP_MAU_10BASE_T_FD 11
#define LLDP_MAU_100BASE_TX_HD 15
#define LLDP_MAU_100BASE_TX_FD 16
#define LLDP_MAU_1000BASE_X_FD 22
#define LLDP_MAU_1000BASE_T_HD 29
#define LLDP_MAU_1000BASE_T_FD 30
#define PHYSR_SPEED(v) ((((v) & 0x0600) >> 7) | (((v) & 0x0030) >> 4))
#define PHYSR_SPEED_10M 0
#define PHYSR_SPEED_100M 1
#define PHYSR_SPEED_1000M 2
#define PHYSR_SPEED_DOWN 7
#define PHYSR_FULL_DUPLEX 0x0008
#define PHY_ANEG_CTRL_ENABLE 0x1000
#define LINKS_CODE_1000M 2
#define PHY_ANEG_ADV_10BASE_T_HD 0x0020
#define PHY_ANEG_ADV_10BASE_T_FD 0x0040
#define PHY_ANEG_ADV_100BASE_TX_HD 0x0080
#define PHY_ANEG_ADV_100BASE_TX_FD 0x0100
#define PHY_GBCR_1000BASE_T_FD 0x0200
#define PORT_MAX_LEN_MASK 0x3fff
#define LLDP_8023_SUBTYPE_MAX_FRAME 4
#define LLDP_AGG_CAPABLE 0x01
#define LLDP_AGG_ACTIVE 0x02
#define LLDP_SYSNAME_TLV_TYPE 5
#define LLDP_SYSDESC_TLV_TYPE 6

#define LLDP_CAPABILITIES_TLV_TYPE 7
#define LLDP_CAPABILITIES_TLV_LENGTH 4
#define LLDP_CAPABILITY_BRIDGE 0x0004

#define LLDP_MGMT_ADDR_TLV_TYPE 8
#define LLDP_MGMT_ADDR_TLV_LENGTH 12
#define LLDP_MGMT_ADDR_STR_LENGTH 5
#define LLDP_MGMT_ADDR_SUBTYPE_IPV4 1
#define LLDP_MGMT_IF_NUMBERING_UNKNOWN 1

#define LLDP_MAX_PAYLOAD 128

void lldp_init(void) __banked;
void lldp_tick(void) __banked;
void lldp_send(void) __banked;

/* Neighbors, per logical port: what the last LLDPDU received there said,
 * until its TTL runs out or the link goes down. Strings are cut to fit and
 * hold printable ASCII only ('"' and '\\' replaced), so JSON can take them. */
#define LLDP_NB_PORTS		(MACHINE_MAX_PORT + 1)
#define LLDP_NB_SYS_LEN		20	// System name, or the chassis ID if it is a string
#define LLDP_NB_PORT_LEN	17	// Port ID (a MAC as aa:bb:cc:dd:ee:ff), else the port description
extern __xdata uint16_t lldp_nb_ttl[LLDP_NB_PORTS];	// Seconds left, 0 = no neighbor
extern __xdata char lldp_nb_sys[LLDP_NB_PORTS][LLDP_NB_SYS_LEN + 1];
extern __xdata char lldp_nb_port[LLDP_NB_PORTS][LLDP_NB_PORT_LEN + 1];
extern __xdata uint8_t lldp_nb_mac[LLDP_NB_PORTS][6];	// Chassis ID if a MAC, else 0
extern __xdata uint8_t lldp_nb_ip[LLDP_NB_PORTS][4];	// IPv4 management address, else 0
void lldp_in(void) __banked;	// A trapped frame to 01:80:c2:00:00:0e in uip_buf
void lldp_show(void) __banked;	// `lldp show`
extern __xdata uint8_t lldp_mgmt_ip[4];	// `lldp addr`, 0.0.0.0 = our own address
extern __xdata uint8_t lldp_chassis[6];	// `lldp chassis`, 0 = our own MAC
void lldp_shutdown(uint16_t ports) __banked;
void lldp_set_addresses(void) __banked;
void lldp_set_rtl_wrapper(void) __banked;
uint8_t lldp_sysname(__xdata uint8_t *p) __banked;
uint8_t lldp_sysdesc(__xdata uint8_t *p) __banked;
uint8_t lldp_portdesc(__xdata uint8_t *p, uint8_t port) __banked;
uint8_t lldp_port_org_tlvs(__xdata uint8_t *p, uint8_t port) __banked __reentrant;

#endif
