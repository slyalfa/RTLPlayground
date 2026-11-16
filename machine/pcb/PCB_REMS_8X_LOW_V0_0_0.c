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
        /* No 2e/15 tune needed: with the +0.95_IND bypass rework (10uF) the far
         * end is clean anywhere from 0xf2c1 to 0xf9c1, including the default. */
        //.sfp_port[0].fixed_mode = SDS_10GR, // testing on good SPF module 
        .sfp_port[0].i2c = I2CBUS( GPIO41_I2C_SDA3_MDIO1, GPIO40_I2C_SCL3_MDC1 ),

        /* Left SFP+ (5) UTX_1 */
        //.sfp_port[1].pin_detect = GPIO38,
        .sfp_port[1].pin_detect = GPIO_NA,
        .sfp_port[1].pin_los = GPIO_NA,
        .sfp_port[1].pin_tx_disable = GPIO_NA,
        .sfp_port[1].sds = 0,
        /* Hardwired 10G link, no cage/EEPROM: bring up SDS 0 as 10GBASE-R at boot */
        .sfp_port[1].fixed_mode = SDS_10GR,
        /* No 2e/15 tune needed: the earlier flapping was supply noise on
         * +0.95_IND (fixed by 10uF instead of 1uF after the ferrite bead);
         * 0xf2c1..0xf5c1 all give a clean link with that rework. */
        // gpio39 == MSDA4
        .sfp_port[1].i2c = I2CBUS( GPIO39_I2C_SDA4, GPIO40_I2C_SCL3_MDC1 ),


        .reset_pin = GPIO_NA,
        /* SCL0/SDA0 (pins 138/142) go to the High chip's slave interface
         * (SSCL/SSDA, pins 136/134), strapped for I2C at 0x5c (sheet 10) */
        .peer_i2c = I2CBUS( GPIO47_I2C_SDA0, GPIO46_I2C_SCL0 ),
        .peer_i2c_addr = 0x5c,
        /* The hardwired 10G link: our port 5 (SDS0) to High's port 6 */
        .peer_link = 5,
        .peer_link_remote = 6,

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

