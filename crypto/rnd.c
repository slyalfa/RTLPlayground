/*
 * Random numbers for session ids and DHCP transaction ids.
 *
 * RAND_NUM1:RAND_NUM0 is a 48-bit LFSR, not an entropy source (doc/registers.md):
 * one output predicts all others. So random numbers come from ChaCha20 instead,
 * keyed from an entropy pool that rnd_mix() fills: the Timer2 count (about
 * 10 MHz) when each frame arrives, its port, length and RX sequence byte, plus
 * the LFSR, the MAC address and the boot timing.
 *
 * Every refill XORs the pool into the key, makes one ChaCha20 block, keeps its
 * first 32 bytes as the next key and hands out the other 32 (fast key
 * erasure): an output tells nothing about the key or about other outputs.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "chacha.h"

#pragma codeseg BANK4
#pragma constseg BANK4

extern __xdata uint8_t sfr_data[4];
extern volatile __xdata uint32_t ticks;
extern __xdata struct chacha20_t chacha20;
extern __xdata uint8_t rnd_pool[RND_POOL_SIZE];
extern __xdata uint8_t rnd_fresh;

static __xdata uint8_t rnd_out[64];
static __xdata uint8_t rnd_left;	// Output bytes not handed out yet, at the end of rnd_out

/* "expand 32-byte k" as chacha20_test() loads it */
static __code const uint8_t rnd_sigma[16] = {
	0x61, 0x70, 0x78, 0x65,  0x33, 0x20, 0x64, 0x6e,
	0x79, 0x62, 0x2d, 0x32,  0x6b, 0x20, 0x65, 0x74
};

static void rnd_block(void)
{
	uint8_t i;

	memcpyc(chacha20.constant, rnd_sigma, sizeof(rnd_sigma));
	for (i = 0; i < RND_POOL_SIZE; i++) {
		chacha20.key[i] ^= rnd_pool[i];
		rnd_pool[i] = 0;
	}
	rnd_fresh = 0;
	chacha20.nonce[0] = ticks;
	chacha20.nonce[1] = ticks >> 8;
	chacha20.nonce[2] = ticks >> 16;
	chacha20.nonce[3] = TL2;
	chacha20.nonce[4] = TH2;

	memset(rnd_out, 0, sizeof(rnd_out));
	chacha20.plaintext = rnd_out;
	chacha20.cyphertext = rnd_out;
	chacha20.length = sizeof(rnd_out);
	chacha20_encrypt();

	memcpy(chacha20.key, rnd_out, 32);
	memset(rnd_out, 0, 32);
	rnd_left = 32;
}

/* Seed the pool at boot, before anything asks for a random number */
void rnd_init(void) __banked
{
	uint8_t i;

	for (i = 0; i < 4; i++) {
		reg_bit_set(RTL837X_RLDP_RLPP, RLDP_RND_EN);
		reg_read_m(RTL837X_RAND_NUM0);
		rnd_mix(sfr_data[0]); rnd_mix(sfr_data[1]);
		rnd_mix(sfr_data[2]); rnd_mix(sfr_data[3]);
		reg_read_m(RTL837X_RAND_NUM1);
		rnd_mix(sfr_data[2]); rnd_mix(sfr_data[3]);
		rnd_mix(TL2);
	}
	for (i = 0; i < 6; i++)
		rnd_mix(uip_ethaddr.addr[i]);
	rnd_mix(ticks); rnd_mix(ticks >> 8);
	rnd_block();
}

/* 32 random bits in sfr_data[0..3] */
void get_random_32(void) __banked
{
	uint8_t i;

	if (rnd_left < 4 || rnd_fresh)
		rnd_block();
	for (i = 0; i < 4; i++) {
		sfr_data[i] = rnd_out[64 - rnd_left];
		rnd_out[64 - rnd_left] = 0;
		rnd_left--;
	}
}
