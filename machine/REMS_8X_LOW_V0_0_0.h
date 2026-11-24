#ifndef _MACHINE_REMS_8X_LOW_V0_0_0_H_
#define _MACHINE_REMS_8X_LOW_V0_0_0_H_

#define MACHINE_MODEL "REMS-8X-LOW_V0.0.0"
#define MACHINE_CHIPSET RTL8372
/* PoE: TPS23880 (U2201, sheet 22) on the bus to the High chip (SDA0/SCL0,
 * see .peer_i2c), RESET on GPIO30 (POE_RST, sheet 9), no interrupt line.
 * Channel 1-8 -> combined port (sheets 14, 15): ch1-4 Low ports 1-4,
 * ch5 port 7, ch6 port 9, ch7 port 8, ch8 not wired. */
#define MACHINE_POE_CHIP	POE_TPS23880
#define POE_TPS_ADDR		0x20
#define POE_I2C_BUS		machine.peer_i2c
#define POE_RESET_GPIO		GPIO30_ACL_BIT3_EN
#define POE_CHANNEL_PORTS	{ 1, 2, 3, 4, 7, 9, 8, 0 }

#endif
