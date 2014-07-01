#include "threads/snapshot.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define CR0_PG 0x80000000
#define OFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *) 0)->MEMBER)
#define INPUT_PAGES 1000

/*It will simply copy the entire address space from {KERN_BASE,Pintos_End}->{Pintos_End,..}*/

void take_snapshot(void)
{
	uint8_t *end=(uint8_t*)(ptov(ram_pages*PGSIZE));
	uint8_t *smemptr=smem_base+INPUT_PAGES*PGSIZE;
	uint8_t *kernbase=(uint8_t *)PHYS_BASE;

	// Lets make the struct frame in the beginning of reserved page in snapshot memory
	struct snap_frame *frame = (struct snap_frame *)(smem_base);
	frame->smem=(uint32_t)smem;
	frame->record_in_begin = smem_base + PGSIZE;
	frame->snapshot_begin = smemptr;
	frame->chunks=2;
	frame->chunk_info[0]=kernbase;
	frame->chunk_info[1]=smem_base;
	frame->chunk_info[2]=smem_base+smem*1024;
	frame->chunk_info[3]=end;

	ASSERT(kernbase<smem_base);
	while(kernbase<smem_base)
		*(smemptr++)=*(kernbase++);

	kernbase+=smem*1024;
	while(kernbase<end)
		*(smemptr++)=*(kernbase++);
	ASSERT(smemptr<=smem_base+smem*1024);	

	frame->record_begin = smemptr;
	frame->record_in_begin = smem_base + PGSIZE;

	//Save all registers
	asm volatile("pushl %ecx");
	asm volatile(
	    "popl %c[ecx](%[frame]) \n\t "
	    "movl %%edi, %c[edi](%[frame]) \n\t"
	    "movl %%esi, %c[esi](%[frame]) \n\t"
	    "movl %%ebp, %c[ebp](%[frame]) \n\t"
	    "movl %%esp, %c[esp](%[frame]) \n\t"
	    "movl %%eax, %c[eax](%[frame]) \n\t"
	    "movl %%ebx, %c[ebx](%[frame]) \n\t"
	    "movl %%edx, %c[edx](%[frame]) \n\t"
	    "call get_eip \n\t"
	    "get_eip: \n\t"
	    "pop %c[eip](%[frame]) \n\t"
	    ::[frame]"c"(frame),
	    [eip]"i"(OFFSETOF(struct snap_frame, registers.eip)),
	    [edi]"i"(OFFSETOF(struct snap_frame, registers.edi)),
	    [esi]"i"(OFFSETOF(struct snap_frame, registers.esi)),
	    [ebp]"i"(OFFSETOF(struct snap_frame, registers.ebp)),
	    [esp]"i"(OFFSETOF(struct snap_frame, registers.esp)),
	    [eax]"i"(OFFSETOF(struct snap_frame, registers.eax)),
	    [ebx]"i"(OFFSETOF(struct snap_frame, registers.ebx)),
	    [ecx]"i"(OFFSETOF(struct snap_frame, registers.ecx)),
	    [edx]"i"(OFFSETOF(struct snap_frame, registers.edx))
	     :"cc", "memory", "eax", "ebx", "edx", "esp");
}

void revert_snapshot_main(void)
{
	struct snap_frame *frame = (struct snap_frame *)(smem_base);
	uint8_t *end=(uint8_t*)(ptov(ram_pages*PGSIZE));
	uint8_t *smemptr=smem_base+INPUT_PAGES*PGSIZE;
	uint8_t *kernbase=(uint8_t *)PHYS_BASE;

	ASSERT(kernbase<smem_base);
	while(kernbase<smem_base) 
	{
		if((*kernbase)!=(*smemptr))
			*(kernbase)=*(smemptr);
		if((unsigned)kernbase%0x10000 == 0) myprintf("Done till %p\n", kernbase);
		kernbase++;
		smemptr++;
	}

	kernbase+=smem*1024;
	while(kernbase<end)
	{
		if((*kernbase)!=(*smemptr))
			*(kernbase)=*(smemptr);
		kernbase++;
		smemptr++;
	}
	ASSERT(smemptr<=smem_base+smem*1024);

	//Load all registers
	rr_mode = REPLAY_MODE;
	asm (
	    "movl %c[edi](%[frame]), %%edi \n\t"
	    "movl %c[esi](%[frame]), %%esi \n\t"
	    "movl %c[ebp](%[frame]), %%ebp \n\t"
	    "movl %c[esp](%[frame]), %%esp \n\t"
	    "movl %c[eax](%[frame]), %%eax \n\t"
	    "movl %c[ebx](%[frame]), %%ebx \n\t"
	    "movl %c[edx](%[frame]), %%edx \n\t"
	    "pushl %c[ecx](%[frame]) \n\t"
	    "popl %%ecx \n\t"
	    "movl %%ebp, %%esp \n\t"
	    "popl %%ebp \n\t"
	    "ret \n\t"
	    ::[frame]"c"(frame),
	    [eip]"i"(OFFSETOF(struct snap_frame, registers.eip)),
	    [edi]"i"(OFFSETOF(struct snap_frame, registers.edi)),
	    [esi]"i"(OFFSETOF(struct snap_frame, registers.esi)),
	    [ebp]"i"(OFFSETOF(struct snap_frame, registers.ebp)),
	    [esp]"i"(OFFSETOF(struct snap_frame, registers.esp)),
	    [eax]"i"(OFFSETOF(struct snap_frame, registers.eax)),
	    [ebx]"i"(OFFSETOF(struct snap_frame, registers.ebx)),
	    [ecx]"i"(OFFSETOF(struct snap_frame, registers.ecx)),
	    [edx]"i"(OFFSETOF(struct snap_frame, registers.edx))
	    :"cc", "memory", "eax", "ebx", "edx", "esp");
	 //Should not come here;
	 ASSERT(1==2);
}

void revert_snapshot()
{
	//We gonna move the esp to free space in the special snapshot page

	//asm ("movl %%esp, %0":"=r"(*newstack));
	asm ("movl %0, %%esp"::"r"(smem_base+PGSIZE-1));
	revert_snapshot_main();
}
