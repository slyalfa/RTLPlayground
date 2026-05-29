
#include <stdint.h>
#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "machine.h"
#include "uip/uip.h"
#include "uip.h"
#include "rtl837x_common.h"

//#pragma codeseg BANK2
//#pragma constseg BANK2
//__xdata uint8_t ntsbuf[TCP_OUTBUF_SIZE];

void nts_init(void) 
//void nts_init(void) __banked
{
        print_string("\nnts_init\n");
       // uip_listen(HTONS(1234));
}
/*
void nts_app(void) {
if(uip_newdata() || uip_rexmit()) {
//uip_send("ok\n", 3);
ntsbuf[0] = (__xdata uint8_t)"o";
ntsbuf[1] = (__xdata 

                uint8_t)"k";
ntsbuf[2] = (__xdata uint8_t)"\n";
uip_send(ntsbuf, 3);
}
}
*/
