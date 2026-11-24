/*
 * Two RTL837x on one board (REM-8x): the managing chip ("Low", holds the
 * management IP) reaches the managed one ("High") over I2C, through High's
 * slave interface, strapped for I2C at machine.peer_i2c_addr. That interface
 * gives register access while High's own CPU keeps running this firmware.
 *
 * Direct register access: peer_reg_read_m() / peer_reg_write_m().
 *
 * Commands for High are run by High's own firmware, because PHY and SerDes
 * access only works from its CPU and its protocol state lives there. They go
 * through a mailbox in High's meter-rate table entries 0-23, which nothing
 * uses (storm control uses 24-59). Each entry holds 24 bits:
 *   entry 0  CTRL, written by Low:  seq[23:16] op[15:8] len[7:0]
 *   entry 1  STAT, written by High: seq[23:16] op[15:8] len[7:0]
 *   entry 2+ data, 3 bytes per entry, first byte in bits 23:16
 * Every CTRL write carries a new seq, and High answers each one with exactly
 * one STAT write carrying the same seq:
 *   REQ       -> ACK                  request chunk, more follow
 *   REQ_LAST  -> RESP / RESP_END/ERR  last request chunk: High runs the
 *                                     command line(s) and returns the output
 *   RESP_ACK  -> RESP / RESP_END/ERR  Low has taken a RESP chunk
 * Low starts each exchange from High's current CTRL seq, so either side can
 * reboot without the two getting out of step.
 *
 * The web interface shows both chips as one switch. Both chips already use
 * the port numbers of the combined switch (machine.log_to_phys_port: Low has
 * 1-4, 9 and 11, High 5-8, 10 and 12 on the REM-8x), so port numbers pass
 * unchanged; High's LAG group n is PEER_LAGS + n. Low runs "json <path>" on
 * High and renumbers the LAG groups in its answer, see jf_putc(). The per-port
 * lists are spliced into Low's own answer; the web interface merges the rest
 * from /peer/<path>.
 */

#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "cmd_parser.h"
#include "machine.h"
#include "timer.h"
#include "peer.h"
#include "httpd/httpd.h"
#include "httpd/page_impl.h"
#include "uip.h"

#pragma codeseg BANK4
#pragma constseg BANK4

extern __code const struct machine machine;
extern __xdata uint8_t sfr_data[4];
extern __xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
extern __xdata uint16_t slen;
extern __xdata uint8_t cmd_capture;
extern __xdata uint8_t cmd_words_len;
extern __xdata uint8_t cmd_words_b[];

#define MBOX_REG(i)		(RTL837X_METER_RATE + ((i) << 2))
#define MBOX_CTRL		MBOX_REG(0)
#define MBOX_STAT		MBOX_REG(1)
#define MBOX_DATA		MBOX_REG(2)
#define MBOX_WORDS		22
#define MBOX_BYTES		(MBOX_WORDS * 3)

#define MBOX_OP_REQ		0x01
#define MBOX_OP_REQ_LAST	0x02
#define MBOX_OP_RESP_ACK	0x03
#define MBOX_OP_ACK		0x81
#define MBOX_OP_RESP		0x82
#define MBOX_OP_RESP_END	0x83	// Last chunk, the command succeeded
#define MBOX_OP_RESP_ERR	0x84	// Last chunk, the command failed
#define MBOX_OP_REJECT		0x85	// Request too long or out of sequence

#define MBOX_STEP_TIMEOUT	(SYS_TICK_HZ * 2)	// Other side reacts from its main loop
#define MBOX_RUN_TIMEOUT	(SYS_TICK_HZ * 15)	// Command runtime, e.g. a cable test

#define MBOX_REQ_SIZE		256

/*
 * Bits 1:0 of RTL837X_REG_I2C_CTRL2 select the clock of the I2C master.
 * Measured on REM-8x with a 1.1 kB transfer: 0 (the default, used for the
 * SFP modules) 0.82 s, 1 0.48 s, 2 0.28 s; 3 is faster still but corrupts
 * bits. Peer transfers use 2 and restore the default afterwards.
 */
#define PEER_I2C_SPEED		2

/* Port numbers of the combined switch: 1-PEER_PORTS, each one on Low or High */
#define PEER_PORTS		MACHINE_PHYS_PORTS
#define PEER_LAGS		4	// LAG groups per chip: High's 1-4 are 5-8

/* JSON filter states */
#define JF_NONE			0
#define JF_STR			1	// In a string, possibly a key
#define JF_KEYEND		2	// After a string, a ':' makes it a key
#define JF_VAL			3	// In the value of a key to renumber
#define JF_ARR			4	// In an array of values to renumber

/* What a renumbered value is */
#define JF_PORT			0	// Physical port, 0 is the CPU
#define JF_LAG			1	// LAG group from 0, 255 is none; jf_lag1: from 1, 0 is none

__xdata uint8_t peer_capture;

__xdata uint8_t mbox_buf[MBOX_BYTES];
__xdata uint8_t mbox_seq;
__xdata uint8_t mbox_len;
__xdata struct timer mbox_timer;

/* Managing side: where the answer goes, see peer_out() */
__xdata uint8_t peer_to_json;
__xdata uint8_t jf_state, jf_klen, jf_kind, jf_num, jf_digits, jf_skip, jf_over, jf_raw, jf_lag1;
__xdata char jf_key[16];

/* Managed side; the managing one also builds its "json" requests in it */
__xdata uint8_t mbox_req[MBOX_REQ_SIZE];
__xdata uint16_t mbox_req_len;
__xdata uint8_t mbox_dead;


static void sfr_word(uint8_t a, uint8_t b, uint8_t c)
{
	sfr_data[0] = 0;
	sfr_data[1] = a;
	sfr_data[2] = b;
	sfr_data[3] = c;
}


/*
 * Register access to the peer through its I2C slave interface. On the bus
 * this is a 2-byte register address, MSB first, then 4 data bytes; the
 * controller assembles the data word, so RTL837X_REG_I2C_OUT holds it in the
 * usual register byte order.
 */
static bool peer_xfer(uint16_t reg, uint8_t write)
{
	uint8_t dev = machine.peer_i2c_addr;
	uint16_t n = 0;
	bool ok;

	if (!dev)
		return false;

	REG_SET(RTL837X_REG_I2C_CTRL2, PEER_I2C_SPEED);
	REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, reg >> 8, reg & 0xff);
	// 2-byte register address, 4 data bytes (length - 1 = 3)
	REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00,
		  0x2 << (I2C_MEM_ADDR_WIDTH - 16) | 3,
		  (dev >> 5) | machine.peer_i2c,
		  ((dev << 3) & 0xff) | (write ? I2C_CTRL_WRITE : 0) | I2C_CTRL_GO);

	do {
		reg_read(RTL837X_REG_I2C_CTRL);
		if (!--n)
			break;	// Controller stuck busy
	} while (SFR_DATA_0 & I2C_CTRL_GO);
	ok = n && !(SFR_DATA_0 & I2C_CTRL_FAIL);

	REG_SET(RTL837X_REG_I2C_CTRL2, 0);
	return ok;
}


/* Read a register of the peer switch into sfr_data, like reg_read_m() */
bool peer_reg_read_m(uint16_t reg) __banked
{
	if (!peer_xfer(reg, 0))
		return false;
	reg_read_m(RTL837X_REG_I2C_OUT);
	return true;
}


/* Write sfr_data to a register of the peer switch, like reg_write_m() */
bool peer_reg_write_m(uint16_t reg) __banked
{
	reg_write_m(RTL837X_REG_I2C_OUT);
	return peer_xfer(reg, 1);
}


/*
 * Managing side
 */

static void jf_emit(uint8_t c)
{
	if (slen < TCP_OUTBUF_SIZE)
		outbuf[slen++] = c;
	else
		jf_over = 1;
}


/* Keys whose values are renumbered, see jf_putc() */
__code const char * __code const jf_port_keys[] = {
	"port", "p", "mirrorPort", 0
};
__code const char * __code const jf_list_keys[] = {
	"allow", "untagged_members", "tagged_members", "pvid", "mirror_rx",
	"mirror_tx", "members", "active_members", "mbr", 0
};
__code const char * __code const jf_lag_keys[] = {
	"lagNum", "lag", 0
};

static bool jf_key_in(__code const char * __code const *keys)
{
	__xdata uint8_t i;
	__code const char *k;

	for (; *keys; keys++) {
		k = *keys;
		for (i = 0; i < jf_klen && k[i] == jf_key[i]; i++)
			;
		if (i == jf_klen && !k[i])
			return true;
	}
	return false;
}


/* End of a number in a value to renumber: write it renumbered */
static void jf_flush(void)
{
	if (!jf_digits)
		return;
	jf_digits = 0;
	if (jf_kind == JF_LAG) {
		if (jf_lag1 ? jf_num != 0 : jf_num != 255)
			jf_num += PEER_LAGS;
	} else if (jf_num > 100) {	// LAG 1-4 in STP, 101-104
		jf_num += PEER_LAGS;
	}				// Ports already have their numbers
	if (jf_num >= 100)
		jf_emit('0' + jf_num / 100);
	if (jf_num >= 10)
		jf_emit('0' + (jf_num / 10) % 10);
	jf_emit('0' + jf_num % 10);
	jf_num = 0;
}


/*
 * Append JSON from High to outbuf. While jf_skip is set, everything up to
 * and including the first '[' is dropped. Port numbers and LAG groups are
 * renumbered for the combined switch: the values of the keys listed above,
 * a single number or an array of them. Strings never contain a '"' here,
 * the JSON writers replace it.
 */
static void jf_putc(uint8_t c)
{
	if (jf_skip) {
		if (c == '[')
			jf_skip = 0;
		return;
	}
	if (jf_state >= JF_VAL) {
		if (c >= '0' && c <= '9') {
			jf_num = jf_num * 10 + c - '0';
			jf_digits = 1;
			return;
		}
		jf_flush();
		if (c == ' ' || (jf_state == JF_ARR && (c == '[' || c == ','))) {
			jf_emit(c);
			return;
		}
		jf_state = JF_NONE;	// The value ends, c is ordinary JSON again
	}
	jf_emit(c);

	if (c == '"') {
		if (jf_state == JF_STR) {
			jf_state = JF_KEYEND;
		} else {
			jf_state = JF_STR;
			jf_klen = 0;
		}
	} else if (jf_state == JF_STR) {
		if (jf_klen < sizeof(jf_key))
			jf_key[jf_klen++] = c;
		else
			jf_klen = 0xff;		// Too long for any of the keys
	} else if (jf_state == JF_KEYEND) {
		jf_state = JF_NONE;
		if (c != ':' || jf_klen > sizeof(jf_key))
			return;
		jf_kind = JF_PORT;
		jf_num = 0;
		jf_digits = 0;
		if (jf_key_in(jf_port_keys)) {
			jf_state = JF_VAL;
		} else if (jf_key_in(jf_list_keys)) {
			jf_state = JF_ARR;
		} else if (jf_key_in(jf_lag_keys)) {
			jf_kind = JF_LAG;
			jf_state = JF_VAL;
		}
	}
}


__xdata uint8_t peer_quiet;	// peer_cmd() shows no output and no errors

static void peer_out(uint8_t c)
{
	if (peer_quiet)
		return;
	if (peer_to_json)
		jf_putc(c);
	else
		write_char(c);
}


/*
 * Run "json <q>[?<http_query>][<fetch_port>]" on High, the query only for
 * jf_raw; its answer goes through jf_putc(). The query ends at a space, like
 * in an HTTP request line.
 */
__xdata uint8_t fetch_port;	// Appended to the path of peer_json_fetch() if not 0

static bool peer_json_fetch(const char *q)
{
	__xdata uint8_t i = 0, r;

	for (const char *p = "json "; *p; p++)
		mbox_req[i++] = *p;
	while (*q && i < MBOX_REQ_SIZE - 4)
		mbox_req[i++] = *q++;
	if (jf_raw && http_query) {
		mbox_req[i++] = '?';
		for (q = http_query; *q && *q != ' ' && i < MBOX_REQ_SIZE - 4; q++)
			mbox_req[i++] = *q;
	}
	if (fetch_port) {
		if (fetch_port >= 10)
			mbox_req[i++] = '0' + fetch_port / 10;
		mbox_req[i++] = '0' + fetch_port % 10;
		fetch_port = 0;
	}
	mbox_req[i] = NUL;

	/* l2.json counts its "lag" from 1, 0 for none; lag.json from 0, 255 for none */
	jf_lag1 = strstart(mbox_req + 5, "/l2.json");
	jf_state = JF_NONE;
	jf_digits = 0;
	jf_over = 0;
	peer_to_json = 1;
	r = peer_cmd(mbox_req);
	peer_to_json = 0;
	jf_flush();
	jf_raw = 0;
	return r == ERR_OK && !jf_over && !jf_skip;
}


/*
 * GET /peer/<path>[?<http_query>]: High's answer for it, with its ports and
 * LAG groups renumbered for the combined switch. The web interface merges
 * what is not a per-port list itself (VLAN, mirroring, LAG). Returns true if
 * there is no peer or it does not answer.
 */
bool peer_json_raw(__xdata uint8_t *path) __banked
{
	if (!machine.peer_i2c_addr)
		return true;
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	jf_skip = 0;
	jf_raw = 1;
	return !peer_json_fetch(path);
}


/* Which chip a port of the combined switch is on: 0 none, 1 Low (ours), 2 High.
 * Ours are the numbers our own tables use, High has the others up to PEER_PORTS. */
static uint8_t peer_side(uint8_t p)
{
	__xdata uint8_t l;

	if (!p || p > PEER_PORTS)
		return 0;
	l = machine.phys_to_log_port[p - 1];
	return MACHINE_PORT_VALID(l) && machine.log_to_phys_port[l] == p ? 1 : 2;
}


/* For /information.json: how the ports of the combined switch are numbered */
void peer_info_json(void) __banked
{
	if (!machine.peer_i2c_addr)
		return;
	__xdata uint8_t i, p, c = '[';

	slen += strtox(outbuf + slen, ",\"peer\":{\"lo\":");
	for (i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {	// Our ports
		p = machine.log_to_phys_port[i];
		outbuf[slen++] = c;
		c = ',';
		if (p >= 10)
			outbuf[slen++] = '0' + p / 10;
		outbuf[slen++] = '0' + p % 10;
	}
	outbuf[slen++] = ']';
	slen += strtox(outbuf + slen, ",\"lags\":");
	outbuf[slen++] = '0' + PEER_LAGS;
	slen += strtox(outbuf + slen, ",\"link\":");
	outbuf[slen++] = '0' + machine.peer_link / 10;
	outbuf[slen++] = '0' + machine.peer_link % 10;
	slen += strtox(outbuf + slen, ",\"plink\":");
	outbuf[slen++] = '0' + machine.peer_link_remote / 10;
	outbuf[slen++] = '0' + machine.peer_link_remote % 10;
	outbuf[slen++] = '}';
}


/*
 * Add High's ports to a per-port JSON answer of ours in outbuf. Our list is
 * the last array in it; High's same answer is spliced in after our last
 * entry, so whatever follows High's list (e.g. "]}") closes the result.
 * If High does not answer, our own answer is left as it was.
 */
void peer_json_merge(__xdata uint8_t *q) __banked
{
	__xdata uint8_t tail[4];
	__xdata uint16_t end = slen, t = slen;
	__xdata uint8_t i;

	if (!machine.peer_i2c_addr)
		return;
	while (t && outbuf[--t] != ']')
		;
	if (end - t > sizeof(tail) || outbuf[t] != ']')
		return;
	for (i = 0; i < end - t; i++)
		tail[i] = outbuf[t + i];

	slen = t;
	if (outbuf[t - 1] != '[')	// Our list is not empty
		outbuf[slen++] = ',';
	jf_skip = 1;
	if (peer_json_fetch(q))
		return;

	slen = t;
	for (i = 0; i < end - t; i++)
		outbuf[slen++] = tail[i];
}


/*
 * counters.json for a port of the combined switch that is one of High's.
 * Returns true if there is no such port, like send_counters().
 */
bool peer_json_counters(uint8_t port) __banked
{
	if (!machine.peer_i2c_addr || peer_side(port) != 2)
		return true;
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	jf_skip = 0;
	fetch_port = port;
	return !peer_json_fetch("/counters.json?port=");
}

/* Write a new CTRL word; the data window must already hold the chunk */
static bool peer_ctrl(uint8_t op, uint8_t len)
{
	mbox_seq++;
	sfr_word(mbox_seq, op, len);
	return peer_reg_write_m(MBOX_CTRL);
}


/* Wait for High's answer to the last CTRL word, return its op or 0 */
static uint8_t peer_stat_wait(uint16_t timeout)
{
	timer_set(&mbox_timer, timeout);
	do {
		if (!peer_reg_read_m(MBOX_STAT))
			return 0;
		if (sfr_data[1] == mbox_seq) {
			mbox_len = sfr_data[3];
			return sfr_data[2];
		}
	} while (!timer_expired(&mbox_timer));
	return 0;
}


/*
 * Run a command line on the peer switch and print its output. Returns
 * ERR_OK, or ERR_INVALID_ARGUMENT if the command failed there or could
 * not be delivered (with a message saying which).
 */
uint8_t peer_cmd(__xdata uint8_t *line) __banked
{
	__xdata uint16_t len = 0;
	__xdata uint8_t n, i, op;

	if (!machine.peer_i2c_addr) {
		print_string("peer: this board has no peer switch\n");
		return ERR_INVALID_ARGUMENT;
	}
	while (line[len])
		len++;

	if (!peer_reg_read_m(MBOX_CTRL))
		goto i2c_err;
	mbox_seq = sfr_data[1];

	do {
		n = len > MBOX_BYTES ? MBOX_BYTES : len;
		for (i = 0; i < n; i += 3) {
			sfr_word(line[i], i + 1 < n ? line[i + 1] : 0, i + 2 < n ? line[i + 2] : 0);
			if (!peer_reg_write_m(MBOX_DATA + ((i / 3) << 2)))
				goto i2c_err;
		}
		line += n;
		len -= n;
		if (!peer_ctrl(len ? MBOX_OP_REQ : MBOX_OP_REQ_LAST, n))
			goto i2c_err;
		// A quiet background command is short: don't stall for the long timeout
		op = peer_stat_wait(len || peer_quiet ? MBOX_STEP_TIMEOUT : MBOX_RUN_TIMEOUT);
	} while (len && op == MBOX_OP_ACK);

	while (op == MBOX_OP_RESP || op == MBOX_OP_RESP_END || op == MBOX_OP_RESP_ERR) {
		n = mbox_len > MBOX_BYTES ? MBOX_BYTES : mbox_len;
		for (i = 0; i < n; i += 3) {
			if (!peer_reg_read_m(MBOX_DATA + ((i / 3) << 2)))
				goto i2c_err;
			mbox_buf[i] = sfr_data[1];
			mbox_buf[i + 1] = sfr_data[2];
			mbox_buf[i + 2] = sfr_data[3];
		}
		for (i = 0; i < n; i++)
			peer_out(mbox_buf[i]);
		if (op == MBOX_OP_RESP_END)
			return ERR_OK;
		if (op == MBOX_OP_RESP_ERR)
			return ERR_INVALID_ARGUMENT;
		if (!peer_ctrl(MBOX_OP_RESP_ACK, 0))
			goto i2c_err;
		op = peer_stat_wait(MBOX_STEP_TIMEOUT);
	}

	if (peer_quiet)
		return ERR_INVALID_ARGUMENT;
	if (op == MBOX_OP_REJECT)
		print_string("\npeer: request rejected (too long?)\n");
	else
		print_string("\npeer: no answer from the peer switch\n");
	return ERR_INVALID_ARGUMENT;

i2c_err:
	if (!peer_quiet)
		print_string("\npeer: I2C error\n");
	return ERR_INVALID_ARGUMENT;
}



/*
 * Commands of the combined switch, on the managing side. Called by
 * cmd_parser() for every command line before it runs it. Commands naming
 * High's ports (5-8, 10, 12) go to High unchanged; commands about
 * the whole switch go to both chips; port lists are split up:
 *   vlan <vid> [name] <ports>  both chips get their own ports, plus the
 *                              internal link as a tagged member, so every
 *                              VLAN crosses between the chips
 *   isolate <port> <ports>     ports on the other chip are reached through
 *                              the internal link, so that one is allowed
 *   ingress <port><mode>...    each chip gets its own ports
 *   mirror <port> <ports>      only within one chip, the other one stops
 *                              mirroring; across the chips is refused
 *   lag <group> <ports>        groups 1-4 on Low's ports are Low's, groups
 *   laghash <group> ...        5-8 on High's ports are High's groups 1-4;
 *   stp lag <group> ...        a group cannot span the chips
 *   lacp off                   both chips
 * Returns true if nothing is left to run here. Otherwise cmd_buffer holds
 * the command for this chip (rewritten if needed, and tokenized).
 */
__xdata uint8_t route_lo[CMD_BUF_SIZE];
__xdata uint8_t route_hi[CMD_BUF_SIZE];
__xdata uint8_t route_lo_n, route_hi_n;
__xdata uint8_t route_end;	// cmd_buffer index after the digits of the last r_port()
__xdata uint8_t r_hi;		// The r_*() helpers write the line for High, else ours
__xdata uint8_t r_line;		// r_copy() goes to the end of the line, else the word

static bool w_is(uint8_t w, __code const char *s)
{
	__xdata uint8_t i;

	if (w >= cmd_words_len)
		return false;
	i = cmd_words_b[w];
	while (*s)
		if (cmd_buffer[i++] != *s++)
			return false;
	return cmd_buffer[i] == ' ' || cmd_buffer[i] == NUL;
}


static bool w_letter(uint8_t w)
{
	uint8_t c = cmd_buffer[cmd_words_b[w]] | 0x20;

	return w < cmd_words_len && c >= 'a' && c <= 'z';
}


/* The port number (1-99) word w starts with, or 0; sets route_end */
static uint8_t r_port(uint8_t w)
{
	__xdata uint8_t i, n = 0, d = 0;

	if (w >= cmd_words_len)
		return 0;
	i = cmd_words_b[w];
	while (cmd_buffer[i] >= '0' && cmd_buffer[i] <= '9' && d < 3) {
		n = n * 10 + cmd_buffer[i++] - '0';
		d++;
	}
	route_end = i;
	return d < 3 ? n : 0;
}


static uint8_t r_side(uint8_t p)
{
	return peer_side(p);
}


static void r_char(uint8_t c)
{
	if (r_hi) {
		if (route_hi_n < CMD_BUF_SIZE - 1)
			route_hi[route_hi_n++] = c;
	} else {
		if (route_lo_n < CMD_BUF_SIZE - 1)
			route_lo[route_lo_n++] = c;
	}
}


/* Copy cmd_buffer from index i up to the end of the word (or the line) */
static void r_copy(uint8_t i)
{
	while (cmd_buffer[i] && (r_line || cmd_buffer[i] != ' '))
		r_char(cmd_buffer[i++]);
	r_line = 0;
}


static void r_word(uint8_t w)
{
	if ((r_hi ? route_hi_n : route_lo_n))
		r_char(' ');
	r_copy(cmd_words_b[w]);
}


/* Port p of the combined switch, followed by the rest of the word after the
 * number (e.g. "t"). Both chips use the combined numbers. */
static void r_port_word(uint8_t p)
{
	r_char(' ');
	if (p >= 10)
		r_char('0' + p / 10);
	r_char('0' + p % 10);
	r_copy(route_end);
}


/* The internal link, tagged if t */
static void r_link(uint8_t t)
{
	__xdata uint8_t p = r_hi ? machine.peer_link_remote : machine.peer_link;

	r_char(' ');
	if (p >= 10)
		r_char('0' + p / 10);
	r_char('0' + p % 10);
	if (t)
		r_char('t');
}


/* The first n words to both chips, then each port word to its chip */
static void r_split(uint8_t n)
{
	__xdata uint8_t w, p;

	for (w = 0; w < n; w++) {
		r_hi = 0;
		r_word(w);
		r_hi = 1;
		r_word(w);
	}
	for (; w < cmd_words_len; w++) {
		p = r_port(w);
		r_hi = r_side(p) == 2;
		if (r_side(p))
			r_port_word(p);
		else
			r_word(w);	// Not a port, let our parser complain
	}
}


bool peer_route(void) __banked
{
	__xdata uint8_t p, side, w, other;

	if (!machine.peer_i2c_addr || !cmd_words_len)
		return false;
	route_lo_n = route_hi_n = 0;
	r_line = 0;

	if (w_is(0, "port") || w_is(0, "mtu") || w_is(0, "pvid") || w_is(0, "storm")) {
		w = 1;
		goto single;
	} else if ((w_is(0, "eee") || w_is(0, "bw")) && cmd_words_len > 2) {
		w = 2;
		goto single;
	} else if (w_is(0, "stp") && w_is(1, "port")) {
		w = 2;
		goto single;
	} else if (w_is(0, "sfp")) {
		p = r_port(1);
		if (p <= machine.n_sfp || p > 2 * machine.n_sfp)
			return false;
		r_hi = 1;
		r_word(0);
		r_char(' ');
		r_char('0' + p - machine.n_sfp);
		r_line = 1;
		r_copy(route_end);
		goto send;
	} else if (w_is(0, "isolate")) {
		p = r_port(1);
		side = r_side(p);
		if (!side)
			return false;
		if (w_is(2, "show") || w_is(2, "off")) {
			w = 1;
			goto single;
		}
		r_hi = side == 2;
		r_word(0);
		r_port_word(p);
		other = 0;
		for (w = 2; w < cmd_words_len; w++) {
			p = r_port(w);
			if (w_is(w, "0") || !r_side(p))
				r_word(w);	// CPU port, or junk for the parser
			else if (r_side(p) == side)
				r_port_word(p);
			else
				other = 1;
		}
		if (other)
			r_link(0);
		goto send;
	} else if (w_is(0, "vlan")) {
		p = cmd_buffer[cmd_words_b[1]];	// A VLAN id, not "show"
		if (cmd_words_len < 3 || p < '0' || p > '9' || w_is(2, "mgmt"))
			return false;
		if (cmd_words_len == 3 && w_is(2, "d"))
			goto both;
		r_split(w_letter(2) ? 3 : 2);
		r_hi = 0;
		r_link(1);
		r_hi = 1;
		r_link(1);
		goto send;
	} else if (w_is(0, "ingress")) {
		if (cmd_words_len < 2)
			return false;
		if (w_letter(1))
			goto both;
		r_split(1);
		goto send;
	} else if (w_is(0, "mirror")) {
		if (w_is(1, "off"))
			goto both;
		side = r_side(r_port(1));
		if (!side)
			return false;
		for (w = 2; w < cmd_words_len; w++) {
			if (r_side(r_port(w)) != side) {
				print_string("mirror: the mirror port and the mirrored ports must be on the same chip, ports 1-4, 9, 11 or 5-8, 10, 12\n");
				err_status = ERR_INVALID_ARGUMENT;
				return true;
			}
		}
		// The other chip stops mirroring
		r_hi = side == 1;
		for (const char *m = "mirror off"; *m; m++)
			r_char(*m);
		if (side == 1)
			goto send_hi;
		r_hi = 1;
		r_word(0);
		for (w = 1; w < cmd_words_len; w++)
			r_port_word(r_port(w));
		goto send;
	} else if (w_is(0, "lag") || w_is(0, "laghash") || (w_is(0, "stp") && w_is(1, "lag"))) {
		w = w_is(0, "stp") ? 2 : 1;
		p = r_port(w);		// The group
		if (!p || p > 2 * PEER_LAGS)
			return false;	// "lag show", or junk for the parser
		side = p > PEER_LAGS ? 2 : 1;
		other = w_is(0, "lag");	// Only lag takes ports
		for (w++; other && w < cmd_words_len; w++) {
			if (r_side(r_port(w)) && r_side(r_port(w)) != side) {
				print_string("lag: groups 1-4 take ports 1-4, 9, 11; groups 5-8 ports 5-8, 10, 12\n");
				err_status = ERR_INVALID_ARGUMENT;
				return true;
			}
		}
		if (side == 1)
			return false;
		r_hi = 1;
		w = w_is(0, "stp") ? 2 : 1;
		for (side = 0; side < w; side++)
			r_word(side);
		r_char(' ');
		r_char('0' + p - PEER_LAGS);
		for (w++; w < cmd_words_len; w++) {
			p = r_port(w);
			if (other && r_side(p))
				r_port_word(p);
			else
				r_word(w);
		}
		goto send;
	} else if ((w_is(0, "l2") && w_is(1, "forget")) || (w_is(0, "eee") && cmd_words_len == 2)
		   || (w_is(0, "lacp") && w_is(1, "off"))
		   || ((w_is(0, "igmp") || w_is(0, "lldp")) && (w_is(1, "on") || w_is(1, "off")))
		   || (w_is(0, "stp") && cmd_words_len > 1 && !w_is(1, "lag") && !w_is(1, "status"))) {
		goto both;
	}
	return false;

single:
	p = r_port(w);
	if (r_side(p) != 2)
		return false;
	r_hi = 1;
	for (side = 0; side < w; side++)
		r_word(side);
	r_port_word(p);
	r_line = 1;
	r_copy(route_end);
	goto send;

both:
	r_hi = 1;
	r_line = 1;
	r_copy(0);
send_hi:
	route_hi[route_hi_n] = NUL;
	if (peer_cmd(route_hi) != ERR_OK)
		err_status = ERR_INVALID_ARGUMENT;
	return false;	// The unchanged command runs here too

send:
	if (route_hi_n) {
		route_hi[route_hi_n] = NUL;
		if (peer_cmd(route_hi) != ERR_OK)
			err_status = ERR_INVALID_ARGUMENT;
	}
	if (!route_lo_n)
		return true;
	for (w = 0; w < route_lo_n; w++)
		cmd_buffer[w] = route_lo[w];
	cmd_buffer[w] = NUL;
	cmd_tokenize();
	return false;
}


/*
 * Managed side
 */

static void mbox_stat(uint8_t op, uint8_t len)
{
	sfr_word(mbox_seq, op, len);
	reg_write_m(MBOX_STAT);
}


static void mbox_data_write(uint8_t n)
{
	for (uint8_t i = 0; i < n; i += 3) {
		sfr_word(mbox_buf[i], mbox_buf[i + 1], mbox_buf[i + 2]);
		reg_write_m(MBOX_DATA + ((i / 3) << 2));
	}
}


void peer_mbox_init(void) __banked
{
	reg_read_m(MBOX_CTRL);
	mbox_seq = sfr_data[1];		// Ignore whatever was there before the boot
	mbox_req_len = 0;
	peer_capture = 0;
}


/* Called from the main loop: take a request chunk, run complete requests */
void peer_mbox_poll(void) __banked
{
	__xdata uint8_t op, len, i;

	reg_read_m(MBOX_CTRL);
	if (sfr_data[1] == mbox_seq)
		return;
	mbox_seq = sfr_data[1];
	op = sfr_data[2];
	len = sfr_data[3];

	if ((op != MBOX_OP_REQ && op != MBOX_OP_REQ_LAST) || len > MBOX_BYTES
	    || mbox_req_len + len >= MBOX_REQ_SIZE) {
		mbox_req_len = 0;
		mbox_stat(MBOX_OP_REJECT, 0);
		return;
	}

	for (i = 0; i < len; i += 3) {
		reg_read_m(MBOX_DATA + ((i / 3) << 2));
		mbox_req[mbox_req_len++] = sfr_data[1];
		if (i + 1 < len)
			mbox_req[mbox_req_len++] = sfr_data[2];
		if (i + 2 < len)
			mbox_req[mbox_req_len++] = sfr_data[3];
	}
	if (op == MBOX_OP_REQ) {
		mbox_stat(MBOX_OP_ACK, 0);
		return;
	}

	mbox_req[mbox_req_len] = NUL;
	mbox_req_len = 0;
	mbox_len = 0;
	mbox_dead = 0;
	peer_capture = 1;
	execute_commands(mbox_req);
	peer_capture = 0;
	if (mbox_dead)
		return;
	mbox_data_write(mbox_len);
	mbox_stat(err_status == ERR_OK ? MBOX_OP_RESP_END : MBOX_OP_RESP_ERR, mbox_len);
}


/*
 * Output of a command run for the peer, from write_char_no_syslog(). A full
 * window is handed over and Low's acknowledgement awaited before going on.
 * This runs in the middle of a print, whose caller may still need the
 * register data it is printing, so sfr_data and the SFR data bytes are kept.
 */
void peer_mbox_putc(char c) __banked
{
	__xdata uint8_t save[8];
	uint8_t i;

	if (mbox_dead)
		return;
	mbox_buf[mbox_len++] = c;
	if (mbox_len < MBOX_BYTES)
		return;

	for (i = 0; i < 4; i++)
		save[i] = sfr_data[i];
	save[4] = SFR_DATA_24; save[5] = SFR_DATA_16; save[6] = SFR_DATA_8; save[7] = SFR_DATA_0;

	mbox_data_write(mbox_len);
	mbox_stat(MBOX_OP_RESP, mbox_len);
	mbox_len = 0;
	mbox_dead = 1;
	timer_set(&mbox_timer, MBOX_STEP_TIMEOUT);
	do {
		reg_read_m(MBOX_CTRL);
		if (sfr_data[1] != mbox_seq) {
			mbox_seq = sfr_data[1];
			if (sfr_data[2] == MBOX_OP_RESP_ACK)
				mbox_dead = 0;
			break;
		}
	} while (!timer_expired(&mbox_timer));

	for (i = 0; i < 4; i++)
		sfr_data[i] = save[i];
	SFR_DATA_24 = save[4]; SFR_DATA_16 = save[5]; SFR_DATA_8 = save[6]; SFR_DATA_0 = save[7];
}


/*
 * The "json <path>" command: print what a GET of <path> would answer,
 * without the HTTP header. This is what Low runs on High to show High's
 * ports in its web interface. The answer is built in outbuf, so this is
 * refused while outbuf holds the answer to a command run over HTTP.
 * A web answer of our own that is still being sent from outbuf would be
 * overwritten; on High nobody else uses the web interface once it is
 * managed through Low.
 */
uint8_t peer_json_local(__xdata uint8_t *q) __banked
{
	__xdata uint16_t save = slen, i;

	if (cmd_capture)
		return ERR_INVALID_ARGUMENT;
	http_query = 0;		// q still has its query
	if (!httpd_json(q)) {
		slen = save;
		print_string("Unknown JSON path\n");
		return ERR_INVALID_ARGUMENT;
	}
	for (i = 0; i + 4 <= slen; i++) {
		if (outbuf[i] == '\r' && outbuf[i + 1] == '\n' && outbuf[i + 2] == '\r' && outbuf[i + 3] == '\n') {
			for (i += 4; i < slen; i++)
				write_char_no_syslog(outbuf[i]);
			break;
		}
	}
	slen = save;
	return ERR_OK;
}


/*
 * LLDP: High has no IP address of its own (see handle_rx()), so it announces
 * ours as the management address of the switch: "lldp addr <our IP>" on
 * High, quietly, whenever we send LLDP ourselves. That also covers a reboot
 * of High and a new address of ours from DHCP.
 */
void peer_lldp_addr(void) __banked
{
	static __code const char * __xdata s;
	__xdata uint8_t i, b;

	if (!machine.peer_i2c_addr)
		return;
	s = "lldp addr ";
	r_hi = 1;
	route_hi_n = 0;
	while (*s)
		r_char(*s++);
	for (i = 0; i < 4; i++) {
		b = ((__xdata uint8_t *)uip_hostaddr)[i];
		if (b >= 100)
			r_char('0' + b / 100);
		if (b >= 10)
			r_char('0' + (b / 10) % 10);
		r_char('0' + b % 10);
		if (i < 3)
			r_char('.');
	}
	route_hi[route_hi_n] = NUL;
	peer_quiet = 1;
	peer_cmd(route_hi);
	peer_quiet = 0;
}


