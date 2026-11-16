#ifndef BOOT_H
#define BOOT_H

void sds_config_mac(uint8_t sds, uint8_t mode) __banked;
void sds_config(uint8_t sds, uint8_t mode) __banked;
void early_boot_handle_button(void) __banked;
void rtl8224_enable(void) __banked;
void nic_setup(void) __banked;
void rtl8373_revision(void) __banked;
void init_smi(void) __banked;
void setup_i2c(void) __banked;
void set_hostname_default(void) __banked;

/* Debug: SerDes register overrides applied inside sds_config() (RAM only,
 * cleared at boot). Managed with the "sdstune" CLI command. */
#define SDS_TUNE_MAX	8
#define SDS_TUNE_FREE	0xff
struct sds_tune {
	uint8_t sds;	// SDS_TUNE_FREE = unused entry
	uint8_t page;
	uint8_t reg;
	uint16_t val;
};
extern __xdata struct sds_tune sds_tune[SDS_TUNE_MAX];
void sds_tune_clear(void) __banked;

#endif
