#include "fifo8.h"

void fifo8_reset(struct fifo8 *p) {
	p->head = 0;
	p->num = 0;
}

int fifo8_used(struct fifo8 *p) {
	return p->num;
}

int fifo8_free(struct fifo8 *p) {
	return p->size - p->num;
}

// CH582 have hardware division instruction, so use % instead &

void fifo8_push(struct fifo8 *p, uint8_t data) {
	p->buf[(p->head + p->num) % p->size] = data;
	p->num++;
}

int fifo8_pop(struct fifo8 *p) {
	uint8_t ret;

	ret = p->buf[p->head++];
	p->head %= p->size;
	p->num--;
	return ret;
}
