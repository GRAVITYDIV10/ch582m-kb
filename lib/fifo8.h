#ifndef _FIFO8_H_
#define _FIFO8_H_
#include <stdint.h>

struct fifo8 {
	uint8_t size;
	uint8_t *buf;
	uint8_t head;
	uint8_t num;
};

void fifo8_reset(struct fifo8 *p);
int fifo8_used(struct fifo8 *p);
int fifo8_free(struct fifo8 *p);
void fifo8_push(struct fifo8 *p, uint8_t data);
int fifo8_pop(struct fifo8 *p);

#endif
