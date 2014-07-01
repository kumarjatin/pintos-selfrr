#include "threads/record.h"
#include "threads/loader.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

void record_init(void)
{
	intr_count=0;
//	lock_init(&intr_lock);
}

/* Using intr_count, save to appropriate place in main memory */
void record_intr(struct intr_frame *frame, uint64_t br_count)
{
//	lock_acquire(&intr_lock);
	intr_count++;
	struct rec_intr *addr = (struct rec_intr*)(smem_base + sizeof(struct rec_intr)*intr_count);
	addr->br_count=br_count;
	addr->int_count=intr_count;
	memcpy((void*)&(addr->int_frame), (void*)frame, sizeof(struct intr_frame));
//	lock_release(&intr_lock);
}

void record_dump_enteries(void)
{
	int i=0;
	struct rec_intr *addr=(struct rec_intr*)(smem_base);
	printf("Dumping Recorded Interrupts(%d)\n",intr_count);
	for(i=0;i<intr_count;i++) {
		ASSERT(i == addr->int_count);
		printf("%d (eip, ecx, br_count)=(%p,%x,%llx)\n",addr->int_count,addr->int_frame.eip, addr->int_frame.ecx, addr->br_count);
		addr++;	
	}
}
