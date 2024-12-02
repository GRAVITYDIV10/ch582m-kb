#ifndef _STEPFORTH_
#define _STEPFORTH_

#include <stdint.h>

void stepforth(void *mctx);

struct sf_task {
	intptr_t ip;
	intptr_t wp;
	intptr_t psp;
	intptr_t psb;
	intptr_t rsp;
};

struct sf_machine {
	struct sf_task *task_addr;
	struct ble_peri_slot *ble_peri_slot_addr;
};

#endif
