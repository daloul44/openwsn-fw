#ifndef __CJOIN_H
#define __CJOIN_H

/**
\addtogroup AppUdp
\{
\addtogroup cjoin
\{
*/
#include "opencoap.h"
//=========================== define ==========================================

//=========================== typedef =========================================

typedef struct {
   coap_resource_desc_t desc;
   opentimer_id_t       timerId;
} cjoin_vars_t;

//=========================== variables =======================================

//=========================== prototypes ======================================

void cjoin_init(void);

/**
\}
\}
*/

#endif
