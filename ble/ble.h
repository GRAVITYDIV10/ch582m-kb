#ifndef _BLE_H_
#define _BLE_H_

#include <stdint.h>
#include "fifo8.h"
#include "stepforth.h"

#define DBG_PRINT(...) PRINT(__VA_ARGS__)

#define PERI_DBG_PRINT(...)             \
	{                               \
		DBG_PRINT("BLE PERI:"); \
		DBG_PRINT(__VA_ARGS__); \
	}
#define PERI_PANIC()                                                         \
	{                                                                    \
		PERI_DBG_PRINT("%s: PANIC LINE %d\n\r", __func__, __LINE__); \
	}

enum {
	CONFIFO_SIZE = 96,
};

struct ble_peri_slot {
	uint16_t state;
	uint8_t taskID;
	uint16_t connHandle;
	uint32_t periodic_cnt;
	uint32_t periodic_delay;

	// virtual forth machine
	struct sf_machine sfm;
	struct sf_task sft;

	// virtual com port
	struct fifo8 conrx_fifo;
	uint8_t conrx_fifo_buf[CONFIFO_SIZE];
	struct fifo8 contx_fifo;
	uint8_t contx_fifo_buf[CONFIFO_SIZE];
};

int ble_peri_slots_find_free(void);
int ble_peri_slots_is_full(void);
int ble_peri_slots_used(void);
int ble_peri_slots_free(void);
int ble_peri_slots_find_by_connHandle(int connHandle);
int ble_peri_slots_find_by_taskID(int task_id);

#endif
