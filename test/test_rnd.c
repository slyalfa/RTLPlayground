/*
 * test_rnd.c - crypto/rnd.c: the entropy pool and fast key erasure.
 *
 * rnd.c is compiled UNMODIFIED. The 8051 ChaCha20 core is assembly, so a C
 * ChaCha20 block (RFC 7539, little-endian words) stands in for
 * chacha20_encrypt(); what is tested is how rnd.c keys it and hands out
 * its output, not the cipher.
 */
#include <stdint.h>
#include <stdio.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "crypto/chacha.h"
#include <test/support.h>

uint8_t sfr_data[4];
volatile uint8_t TL2, TH2;	/* Timer2 count */
volatile uint32_t ticks;
struct uip_eth_addr uip_ethaddr = { .addr = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 } };
struct chacha20_t chacha20;
uint8_t rnd_pool[RND_POOL_SIZE];
uint8_t rnd_fresh;
static uint8_t rnd_pos;
static int blocks;

/* The firmware's rnd_mix() lives in rtlplayground.c; the same here */
void rnd_mix(uint8_t b)
{
	uint8_t p = rnd_pool[rnd_pos];
	rnd_pool[rnd_pos] = (uint8_t)((p << 1) | (p >> 7)) ^ b;
	rnd_pos = (rnd_pos + 1) & (RND_POOL_SIZE - 1);
	if (rnd_fresh != 0xff)
		rnd_fresh++;
}

/* The LFSR registers: a fixed sequence, as if reseeded the same at reset */
static uint32_t lfsr = 0x12345678;
void reg_bit_set(uint16_t a, char b) { (void)a; (void)b; lfsr = lfsr * 1103515245u + 12345u; }
void reg_read_m(uint16_t a) { (void)a; sfr_data[0] = lfsr >> 24; sfr_data[1] = lfsr >> 16; sfr_data[2] = lfsr >> 8; sfr_data[3] = lfsr; }
void memcpyc(uint8_t *d, const uint8_t *s, uint16_t n) { memcpy(d, s, n); }

#define ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QR(a, b, c, d) a += b; d ^= a; d = ROTL(d, 16); c += d; b ^= c; b = ROTL(b, 12); \
		       a += b; d ^= a; d = ROTL(d, 8); c += d; b ^= c; b = ROTL(b, 7)
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
void chacha20_encrypt(void)
{
	uint32_t in[16], x[16];
	uint8_t ks[64];
	blocks++;
	for (int i = 0; i < 4; i++) in[i] = le32(chacha20.constant + 4 * i);
	for (int i = 0; i < 8; i++) in[4 + i] = le32(chacha20.key + 4 * i);
	in[12] = le32(chacha20.cnt);
	for (int i = 0; i < 3; i++) in[13 + i] = le32(chacha20.nonce + 4 * i);
	memcpy(x, in, sizeof(x));
	for (int r = 0; r < 10; r++) {
		QR(x[0], x[4], x[8], x[12]); QR(x[1], x[5], x[9], x[13]);
		QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
		QR(x[2], x[7], x[8], x[13]); QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++) {
		uint32_t v = x[i] + in[i];
		ks[4 * i] = v; ks[4 * i + 1] = v >> 8; ks[4 * i + 2] = v >> 16; ks[4 * i + 3] = v >> 24;
	}
	for (int i = 0; i < chacha20.length; i++)
		chacha20.cyphertext[i] = chacha20.plaintext[i] ^ ks[i];
}

static uint32_t r32(void)
{
	get_random_32();
	return (uint32_t)sfr_data[0] << 24 | sfr_data[1] << 16 | sfr_data[2] << 8 | sfr_data[3];
}

static int all_zero(const uint8_t *p, int n)
{
	while (n--)
		if (*p++)
			return 0;
	return 1;
}

int main(void)
{
	uint8_t key[32];
	uint32_t v[64];

	rnd_init();
	CHECK(blocks == 1 && all_zero(rnd_pool, RND_POOL_SIZE) && !rnd_fresh, "rnd_init keys ChaCha20 once and empties the pool");
	CHECK(!all_zero(chacha20.key, 32), "the key is set");

	/* No new entropy: 8 numbers per block, the key moves on per block */
	memcpy(key, chacha20.key, 32);
	blocks = 0;
	for (int i = 0; i < 8; i++)
		v[i] = r32();
	CHECK(blocks == 0, "8 numbers come out of the block rnd_init made");
	v[8] = r32();
	CHECK(blocks == 1 && memcmp(key, chacha20.key, 32), "the 9th makes a new block with a new key");

	/* Entropy arrives: the next number makes a new block at once */
	blocks = 0;
	rnd_mix(0x5a);
	v[9] = r32();
	CHECK(blocks == 1 && all_zero(rnd_pool, RND_POOL_SIZE), "a new frame's entropy goes into the key at once");

	for (int i = 10; i < 64; i++)
		v[i] = r32();
	int dup = 0;
	for (int i = 0; i < 64; i++)
		for (int j = i + 1; j < 64; j++)
			dup += v[i] == v[j];
	CHECK(dup == 0, "64 numbers, no repeats");

	/* The output looks uniform */
	int ones = 0;
	for (int i = 0; i < 64; i++)
		ones += __builtin_popcount(v[i]);
	CHECK(ones > 900 && ones < 1150, "about half the bits are set");

	/* Same LFSR seed and same boot: different pools still give different numbers */
	uint32_t a, b;
	memset(&chacha20, 0, sizeof(chacha20)); memset(rnd_pool, 0, sizeof(rnd_pool)); rnd_fresh = 0;
	lfsr = 0x12345678; ticks = 100;
	rnd_init(); a = r32();
	memset(&chacha20, 0, sizeof(chacha20)); memset(rnd_pool, 0, sizeof(rnd_pool)); rnd_fresh = 0;
	lfsr = 0x12345678; ticks = 100;
	rnd_mix(0x01);	/* one frame arrived at a different time */
	rnd_init(); b = r32();
	CHECK(a != b, "one byte of entropy changes the output");

	printf("rnd: %d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
