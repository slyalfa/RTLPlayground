#define MACHINE_CHIPSET RTL8372
#include "machine_defs.h"
__code const struct machine machine = {
	MACHINE_COMMON_SETTING,
       // .machine_name = "Thirdwave REMs Low ",
        .n_sfp = 2,
	.n_10g = 0,
        /* Numbered as one switch with the High chip: copper 1-8, SFP+ 9-10,
         * the backplane 11-12. Ours: copper 1-4, SFP+ 9, backplane 11 */
        .log_to_phys_port = {0, 0, 0, 11, 1, 2, 3, 4, 9},
        .phys_to_log_port = {4, 5, 6, 7, 0, 0, 0, 0, 8, 0, 3, 0},
        .is_sfp = {0, 0, 0, 2, 0, 0, 0, 0, 1},

        /* Right SFP+ (9) UTX_0 */
        .sfp_port[0].pin_detect = GPIO37,
        .sfp_port[0].pin_los = GPIO_NA,
        .sfp_port[0].pin_tx_disable = GPIO_NA,
        .sfp_port[0].sds = 1,
        .sfp_port[0].i2c = I2CBUS( GPIO41_I2C_SDA3_MDIO1, GPIO40_I2C_SCL3_MDC1 ),

        /* Backplane (11) UTX_1 */
        .sfp_port[1].pin_detect = GPIO_NA,
        .sfp_port[1].pin_los = GPIO_NA,
        .sfp_port[1].pin_tx_disable = GPIO_NA,
        .sfp_port[1].sds = 0,
        /* Hardwired 10G link, no cage/EEPROM: bring up SDS 0 as 10GBASE-R at boot */
        .sfp_port[1].fixed_mode = SDS_10GR,
        // gpio39 == MSDA4
        .sfp_port[1].i2c = I2CBUS( GPIO39_I2C_SDA4, GPIO40_I2C_SCL3_MDC1 ),


        .reset_pin = GPIO_NA,
        /* SCL0/SDA0 (pins 138/142) go to the High chip's slave interface
         * (SSCL/SSDA, pins 136/134), strapped for I2C at 0x5c (sheet 10) */
        .peer_i2c = I2CBUS( GPIO47_I2C_SDA0, GPIO46_I2C_SCL0 ),
        .peer_i2c_addr = 0x5c,
        /* The hardwired 10G backplane: our port 11 (SDS0) to High's port 12 */
        .peer_link = 11,
        .peer_link_remote = 12,

        /* This Turns on the SYS LED
         * It seems the SYS LED is not used for heartbeat */
        .high_leds = {
                .mux = LED_28_SYS,
                .enable = LED_28_SYS
        },
        .port_led_set = { 0, 0, 0, 1, 0, 0, 0, 0, 1},
        /* Spare LEDs, lit while the port delivers PoE: port 1 D1125 (LED_6),
         * port 2 D1104 (LED_4), port 3 D1123 (LED_3), port 4 D1124 (LED_0) */
        .port_xled = { 0, 0, 0, 0, XLED(6), XLED(4), XLED(3), XLED(0), 0 },
        .led_sets = {
                {       /* Set 0 for RJ45 connectors */

                        /* LED0 */
                        LEDS_2G5 | LEDS_1G | LEDS_100M | LEDS_10M | LEDS_LINK | LEDS_ACT | LEDS_10G,

                        /* LED1 */
                        LEDS_2G5 | LEDS_LINK | LEDS_10G,

                        /* LED2 */
                        LEDS_1G | LEDS_LINK,

                        /* LED3 */
                        0
                },
                {       /* Set 1 for SFP+ connectors */

                        /* LED0 LED24_port8 LED9_port3  */
                        LEDS_2G5 | LEDS_1G | LEDS_100M | LEDS_10M | LEDS_LINK | LEDS_ACT | LEDS_10G,

                        /* LED1 LED25_port8 LED10_port3*/
                        LEDS_2G5 | LEDS_LINK | LEDS_10G,

                        /* LED2 */
                        0,

                        /* LED3 */
                       0
                },
        },
};

