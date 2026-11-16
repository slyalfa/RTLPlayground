/* High switch of the REM-8x board: same pinout as the Low switch, but port 6
 * (SDS 1) is the hardwired link and port 5 (SDS 0) is a real SFP+ cage. */
#include "machine_defs.h"
__code const struct machine machine = {
	MACHINE_COMMON_SETTING,
       // .machine_name = "Thirdwave REMs",
       // .isRTL8373 = 0,
       // .min_port = 3,
       // .max_port = 8,
        .n_sfp = 2,
	.n_10g = 0,
        .log_to_phys_port = {0, 0, 0, 5, 1, 2, 3, 4, 6},
        .phys_to_log_port = {4, 5, 6, 7, 3, 8, 0, 0, 0},
        .is_sfp = {0, 0, 0, 2, 0, 0, 0, 0, 1},

        /* Right SFP+ (6) UTX_0 */
        .sfp_port[0].pin_detect = GPIO37,
        //.sfp_port[0].pin_detect = GPIO_NA,// for test ing 
        .sfp_port[0].pin_los = GPIO_NA,
        .sfp_port[0].pin_tx_disable = GPIO_NA,
        .sfp_port[0].sds = 1,
        /* Switch-to-switch link to the Low chip's SDS 0 (sheet 18), no cage/EEPROM:
         * bring up SDS 1 as 10GBASE-R at boot */
        .sfp_port[0].fixed_mode = SDS_10GR,
        /* No 2e/15 tune needed: the earlier flapping was supply noise on
         * +0.95_IND_B (fixed by 10uF instead of 1uF after the ferrite bead);
         * 0xf2c1..0xf5c1 all give a clean link with that rework. */
        //.sfp_port[0].fixed_mode = SDS_10GR, // testing on good SPF module 
        .sfp_port[0].i2c = I2CBUS( GPIO41_I2C_SDA3_MDIO1, GPIO40_I2C_SCL3_MDC1 ),

        /* Left SFP+ (5) UTX_1 */
        //.sfp_port[1].pin_detect = GPIO38,
        .sfp_port[1].pin_detect = GPIO_NA,
        .sfp_port[1].pin_los = GPIO_NA,
        .sfp_port[1].pin_tx_disable = GPIO_NA,
        .sfp_port[1].sds = 0,
        // gpio39 == MSDA4
        .sfp_port[1].i2c = I2CBUS( GPIO39_I2C_SDA4, GPIO40_I2C_SCL3_MDC1 ),


        .reset_pin = GPIO_NA,
        /* Managed by the Low chip over I2C (our slave interface, pins 134/136,
         * strapped for I2C at 0x5c): run its commands, see peer.c */
        .peer_mailbox = 1,
        /* The hardwired 10G link: our port 6 (SDS1) to Low's port 5 */
        .peer_link = 6,
        .peer_link_remote = 5,

        /* This Turns on the SYS LED
         * It seems the SYS LED is not used for heartbeat */
        .high_leds = {
                .mux = LED_28_SYS,
                .enable = LED_28_SYS
        },
        .port_led_set = { 0, 0, 0, 1, 0, 0, 0, 0, 1},
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

