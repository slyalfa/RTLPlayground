/*
 * test_igmp.c - IGMP reports trapped to the CPU, untagged and 802.1Q tagged.
 *
 * A trapped IGMP frame reaches the CPU in its ingress tagging: an untagged
 * report has no 802.1Q tag, a VID 1 or priority-tagged report keeps it
 * (measured on RTL8372 and RTL8373). rtl837x_igmp.c is compiled UNMODIFIED;
 * the table engine is the register mock in hw_mock.c.
 */
#include <stdint.h>
#include <stdio.h>

#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_igmp.h"
#include "machine.h"
#include "uip.h"
#include <test/support.h>
#include <test/hw_mock.h>

uint8_t  uip_buf[UIP_CONF_BUFFER_ALLOC_SIZE];
uint16_t uip_len;
uint8_t  igmpEnabled = 1;
uint8_t  cpuPort = 9;
const struct machine machine;
struct machine_runtime machine_detected = { .isRTL8373 = 1 };
void print_sfr_data(void) { }
void print_byte(uint8_t v) { (void)v; }
void print_short(uint16_t v) { (void)v; }

/* Build a trapped frame: DA, SA, RTL tag (RX: port in the low nibble of the
 * last word), optional 802.1Q tag, IPv4 header with Router Alert, IGMP. */
static uint16_t frame(uint8_t port, int tagged, uint16_t tci, uint8_t type,
		      const uint8_t grp[4])
{
	uint16_t n = 0;
	t_memset(uip_buf, 0, sizeof(uip_buf));
	static const uint8_t da[6] = { 0x01, 0x00, 0x5e, 0x01, 0x02, 0x03 };
	static const uint8_t sa[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
	for (int i = 0; i < 6; i++) uip_buf[n++] = da[i];
	for (int i = 0; i < 6; i++) uip_buf[n++] = sa[i];
	uip_buf[n++] = 0x88; uip_buf[n++] = 0x99;	/* RTL tag */
	uip_buf[n++] = 0x04; uip_buf[n++] = 0x00;
	uip_buf[n++] = 0x00; uip_buf[n++] = 0x00;
	uip_buf[n++] = 0x00; uip_buf[n++] = port;
	if (tagged) {
		uip_buf[n++] = 0x81; uip_buf[n++] = 0x00;
		uip_buf[n++] = tci >> 8; uip_buf[n++] = tci;
	}
	uip_buf[n++] = 0x08; uip_buf[n++] = 0x00;	/* IPv4 */
	uint16_t ip = n;
	uip_buf[n++] = 0x46;				/* IHL 6: Router Alert */
	n += 8;
	uip_buf[ip + 8] = 1;				/* TTL */
	uip_buf[ip + 9] = IGMP_PROTO;
	n = ip + 24;					/* 20 + 4 option bytes */
	uip_buf[ip + 20] = 0x94; uip_buf[ip + 21] = 0x04;
	uip_buf[n] = type;
	for (int i = 0; i < 4; i++) uip_buf[n + 4 + i] = grp[i];
	return n + 8;
}

/* The L3MC entry the handler wrote for group grp, by its word B, or 0 */
static const struct hw_l2 *group_entry(const uint8_t grp[4])
{
	uint32_t b = ((uint32_t)(grp[0] & 0xf) << 24) | 0x10000000u |
		     ((uint32_t)grp[1] << 16) | ((uint32_t)grp[2] << 8) | grp[3];
	for (int i = 0; i < hw_l2_count(); i++) {
		const struct hw_l2 *e = hw_l2_at(i);
		if ((e->b & 0x3fffffffu) == b)
			return e;
	}
	return 0;
}

static uint16_t pmask_of(const struct hw_l2 *e)
{
	return (uint16_t)((e->b >> 30) | ((e->c & 0xff) << 2));
}

static void join_case(const char *what, int tagged, uint16_t tci)
{
	static const uint8_t grp[4] = { 239, 1, 2, 3 };
	char msg[96];
	hw_reset();
	uip_len = frame(3, tagged, tci, IGMP_V2_REPORT, grp);
	uint8_t r = igmp_packet_handler();
	snprintf(msg, sizeof(msg), "%s: handled as IGMP", what);
	CHECK(r == 1, msg);
	const struct hw_l2 *e = group_entry(grp);
	snprintf(msg, sizeof(msg), "%s: group 239.1.2.3 entry written", what);
	CHECK(e != 0, msg);
	snprintf(msg, sizeof(msg), "%s: member mask is the ingress port", what);
	CHECK(e && pmask_of(e) == (1u << 3), msg);

	/* A second port joins, then the first leaves */
	uip_len = frame(4, tagged, tci, IGMP_V2_REPORT, grp);
	igmp_packet_handler();
	e = group_entry(grp);
	snprintf(msg, sizeof(msg), "%s: second port added", what);
	CHECK(e && pmask_of(e) == ((1u << 3) | (1u << 4)), msg);
	uip_len = frame(3, tagged, tci, IGMP_V2_LEAVE, grp);
	igmp_packet_handler();
	e = group_entry(grp);
	snprintf(msg, sizeof(msg), "%s: leave removes only that port", what);
	CHECK(e && pmask_of(e) == (1u << 4), msg);
}

int main(void)
{
	join_case("untagged", 0, 0);
	join_case("VID 1", 1, 0x0001);
	join_case("priority tag", 1, 0xa000);

	/* Not IGMP: a tagged UDP frame to a group goes on to the IP stack */
	static const uint8_t grp[4] = { 239, 1, 2, 3 };
	hw_reset();
	uip_len = frame(3, 1, 1, IGMP_V2_REPORT, grp);
	uip_buf[12 + 8 + 4 + 2 + 9] = 17;
	CHECK(igmp_packet_handler() == 0, "tagged non-IGMP frame passed on");
	CHECK(hw_l2_count() == 0, "no entry for a non-IGMP frame");

	/* 224.0.0.0/24 stays flooded, tagged or not */
	static const uint8_t local[4] = { 224, 0, 0, 251 };
	hw_reset();
	uip_len = frame(3, 1, 1, IGMP_V2_REPORT, local);
	CHECK(igmp_packet_handler() == 1 && hw_l2_count() == 0, "tagged 224.0.0.251 report ignored");

	printf("IGMP: %d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
