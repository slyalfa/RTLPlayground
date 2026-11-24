#ifndef _PEER_H_
#define _PEER_H_

#include <stdint.h>
#include <stdbool.h>

/* Set while a command from the peer switch runs, see write_char_no_syslog() */
extern __xdata uint8_t peer_capture;

/* Managing side (machine.peer_i2c_addr set) */
bool peer_reg_read_m(uint16_t reg) __banked;
bool peer_reg_write_m(uint16_t reg) __banked;
uint8_t peer_cmd(__xdata uint8_t *line) __banked;
void peer_lldp_addr(void) __banked;	// High announces our IP over LLDP
void peer_json_merge(__xdata uint8_t *q) __banked;
bool peer_json_counters(uint8_t port) __banked;
bool peer_json_raw(__xdata uint8_t *path) __banked;
void peer_info_json(void) __banked;
bool peer_route(void) __banked;

/* Managed side (machine.peer_mailbox set) */
void peer_mbox_init(void) __banked;
void peer_mbox_poll(void) __banked;
void peer_mbox_putc(char c) __banked;

/* Either side: print the answer of a JSON endpoint, see httpd_json() */
uint8_t peer_json_local(__xdata uint8_t *q) __banked;

#endif
