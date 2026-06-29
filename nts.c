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
//__xdata uint8_t ntsbuf[TCP_OUTBUF_SIZE];

#define TSTATE_NONE             0
#define TSTATE_TX               1
#define TSTATE_ACKED            2
#define TSTATE_CLOSED           3
#define TSTATE_POST             4
#define TSTATE_MULTIPART        5
// Position in the serial buffer
//__xdata uint8_t l;
__xdata volatile uint8_t sbuf_ptr_tail;


void nts_init(void) 
        //void nts_init(void) __banked
{
        __xdata struct nts_state * __xdata s = &(uip_conn->appstate);
        print_string("\n################nts_initi_func###########\n");
        uip_listen(HTONS(10001));
        s->tstate = TSTATE_CLOSED;
        sbuf_ptr_tail = sbuf_ptr;
}
void nts_serial_rx(void)
{
        __xdata uint8_t ntsbuf[TCP_OUTBUF_SIZE];
        // If tail = head the buffer is empty
        // sbuf_ptr is the head

        while (sbuf_ptr_tail != sbuf_ptr)
        {
                // print_string_nts("\nsbuf print\n");

                // Echo back
                write_char_nts(sbuf[sbuf_ptr_tail ]); 
                // modulo count to SBUF_SIZE
                sbuf_ptr_tail = (sbuf_ptr_tail +1) & (SBUF_SIZE - 1);
                ntsbuf[0] = 0x4f; // "O"
                                  //ntsbuf[1] = (__xdata uint8_t)"k";
                ntsbuf[1] = 0x4b; // "K"
                                  //ntsbuf[2] = (__xdata uint8_t)"\n";
                ntsbuf[2] = 0x0a; // "\n"
                uip_send(ntsbuf, 3);


        }
}
