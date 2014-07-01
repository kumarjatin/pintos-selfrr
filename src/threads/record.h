#ifndef THREADS_RECORD_H
//123124324098
#define THREADS_RECORD_H
#include <stdint.h>
#include <string.h>
#include "threads/snapshot.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
/*
In physical memory looks like(increasing memory order):
  * Kernel Pages
  * User pages
  * Record Pages <-- Being used for saving interrupts
*/
#define RECORD_INTR(frame, br_count)	\
	struct rec_intr *addr = (struct rec_intr*)(smem_base + sizeof(struct rec_intr)*intr_count);	\
	addr->br_count=br_count;	\
	addr->int_count=intr_count;	\
	memcpy((void*)&(addr->int_frame), (void*)frame, sizeof(struct intr_frame));	\
	intr_count++;

extern uint32_t intr_count;
//static struct lock intr_lock;	/* Lock to be used for storing interrupts */

struct rec_intr {
	struct intr_frame int_frame;	/* Complete Interrupt Frame */
	uint64_t br_count;		/* 64Bit Branch Count	*/
	uint32_t int_count;		/* Interrupt No, Extra for debugging */
};

void record_intr(struct intr_frame *f, uint64_t br_count);
void record_dump_enteries(void);

#endif
