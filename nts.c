
#include "nts.h"
#include <stdint.h>
#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "machine.h"
#include "uip/uip.h"
#include "uip.h"
#include "rtl837x_common.h"

//#pragma codeseg BANK2
//#pragma constseg BANK2
__xdata uint8_t ntsbuf[TCP_OUTBUF_SIZE];

#define TSTATE_NONE             0
#define TSTATE_TX               1
#define TSTATE_ACKED            2
#define TSTATE_CLOSED           3
#define TSTATE_POST             4
#define TSTATE_MULTIPART        5

void nts_init(void) 
        //void nts_init(void) __banked
{
        __xdata struct nts_state * __xdata s = &(uip_conn->appstate);
        print_string("\n################nts_initi_func###########\n");
        uip_listen(HTONS(1234));
        s->tstate = TSTATE_CLOSED;
}

void nts_appcall(void) {
        if(uip_newdata() || uip_rexmit()) {
                print_string("\n################nts_app###########\n");
                //uip_send("ok\n", 3);
                ntsbuf[0] = (__xdata uint8_t)"o";
                ntsbuf[1] = (__xdata 

                                uint8_t)"k";
                ntsbuf[2] = (__xdata uint8_t)"\n";
                uip_send(ntsbuf, 3);
        }
}

