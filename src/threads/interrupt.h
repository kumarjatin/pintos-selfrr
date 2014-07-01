#ifndef THREADS_INTERRUPT_H
//89071147255670685
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/flags.h"
#include "threads/perf.h"

#define TRACE_FLAG 	0x100	/* Trace flag in CPU EFLAGS register */

/* Interrupts on or off? */
enum intr_level 
  {
    INTR_OFF,             /* Interrupts disabled. */
    INTR_ON               /* Interrupts enabled. */
  };

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct intr_frame
  {
    /* Pushed by intr_entry in intr-stubs.S.
       These are the interrupted task's saved registers. */
    uint32_t edi;               /* Saved EDI. */
    uint32_t esi;               /* Saved ESI. */
    uint32_t ebp;               /* Saved EBP. */
    uint32_t esp_dummy;         /* Not used. */
    uint32_t ebx;               /* Saved EBX. */
    uint32_t edx;               /* Saved EDX. */
    uint32_t ecx;               /* Saved ECX. */
    uint32_t eax;               /* Saved EAX. */
    uint16_t gs, :16;           /* Saved GS segment register. */
    uint16_t fs, :16;           /* Saved FS segment register. */
    uint16_t es, :16;           /* Saved ES segment register. */
    uint16_t ds, :16;           /* Saved DS segment register. */

    /* Pushed by intrNN_stub in intr-stubs.S. */
    uint32_t vec_no;            /* Interrupt vector number. */

    /* Sometimes pushed by the CPU,
       otherwise for consistency pushed as 0 by intrNN_stub.
       The CPU puts it just under `eip', but we move it here. */
    uint32_t error_code;        /* Error code. */

    /* Pushed by intrNN_stub in intr-stubs.S.
       This frame pointer eases interpretation of backtraces. */
    void *frame_pointer;        /* Saved EBP (frame pointer). */

    /* Pushed by the CPU.
       These are the interrupted task's saved registers. */
    void (*eip) (void);         /* Next instruction to execute. */
    uint16_t cs, :16;           /* Code segment for eip. */
    uint32_t eflags;            /* Saved CPU flags. */
    void *esp;                  /* Saved stack pointer. */
    uint16_t ss, :16;           /* Data segment for esp. */
  };

extern int loop_counter;
extern int enable_record;
extern int enable_replay;
extern int pmi_expected;

struct record_loop {
	struct intr_frame frame;
	uint32_t counter;
	long long int branches;
	int extra;
	int vec_no;
};

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
void my_intr_dump_frame(const struct intr_frame *);
const char *intr_name (uint8_t vec);

void intr_irq_mask(int irq);
void intr_irq_unmask(int irq);

bool intr_is_registered ( uint8_t vec );
void intr_record_flag(int val);
void disable_intrerrupts_replay(void);
void intr_replay_interrupt(struct intr_frame *frame, int cntr_enabled);
void intr_debug_handler(struct intr_frame *frame);
void intr_debug_handler_out(struct intr_frame *frame);
void intr_pmi_handler(struct intr_frame *frame);
int intr_setup_next_replay(int set_db);
void destroy_intr_handlers(void);
void restore_intr_handlers(void);

//#pragma optimize( "", off )
#define INTR_GET_LEVEL(_level)\
	asm volatile ("pushfl; popl %0":"=g" (flags));\
	_level = (((flags&FLAG_IF)>>9)&INTR_ON) | ((!((flags&FLAG_IF)>>9))&INTR_OFF);

#define INTR_CONTEXT(context)\
	context = intr_context();\

#define INTR_ENABLE\
	INTR_CONTEXT(context);\
	/*TODO: ASSERT Take 1 branch */\
	ASSERT (!context);\
	asm volatile ("sti");

#define INTR_DISABLE\
	asm volatile ("cli":::"memory");

#define INTR_SET_LEVEL(_level)\
	/*TODO: Takes 1 branch. Prefer to use assembly code like in init.c */\
	if(_level == INTR_ON)\
		{INTR_ENABLE;}\
	else {INTR_DISABLE;};

#define INTR_SET_LEVEL_ENABLE_COUNTER(_level, glow, ghigh)\
	if(_level == INTR_ON)\
	{\
		INTR_CONTEXT(context);\
		ASSERT (!context);\
		if(glow)\
		{\
			INCREMENT_COUNTER(-1);\
		}\
		ENABLE_COUNTERS1(glow, ghigh);\
		asm volatile ("sti");\
		/*JMP instruction here causing one branch so -1 above */\
	}\
	else\
	{\
		INTR_DISABLE;\
		ENABLE_COUNTERS1(glow, ghigh);\
	};

#define INTR_SET_LEVEL_ENABLE_COUNTER_DEBUG(_level, glow, ghigh, eflags)\
	if(_level == INTR_ON)\
	{\
		INTR_CONTEXT(context);\
		/*TODO: ASSERT Take 1 branch */\
		ASSERT (!context);\
		if(glow)\
		{\
			INCREMENT_COUNTER(-1);\
		}\
		ENABLE_COUNTERS1(glow, ghigh);\
		SET_DEBUG_FLAG(eflags); \
		asm volatile ("nop \n\t" \
					  "sti \n\t");\
		/*TODO: JMP instruction here causing one branch */\
	}\
	else\
	{\
		INTR_DISABLE;\
		ENABLE_COUNTERS1(glow, ghigh);\
	};

#define MY_INTR_DUMP_FRAME(f)	\
	do{				\
		asm ("movl %%cr2, %0":"=r" (__cr2));	\
		MYNEWPRINTF ("Interrupt %#04x (%s) at eip=%p, EFLAGS=%x\n",			\
				f->vec_no, intr_names[f->vec_no], f->eip, f->eflags);		\
		MYNEWPRINTF (" cr2=%08" PRIx32 " error=%08" PRIx32 "\n", __cr2, f->error_code);	\
		MYNEWPRINTF (" eax=%08" PRIx32 " ebx=%08" PRIx32 " ecx=%08" PRIx32 " edx=%08"	\
				PRIx32 "\n", f->eax, f->ebx, f->ecx, f->edx);			\
		MYNEWPRINTF (" esi=%08" PRIx32 " edi=%08" PRIx32 " esp=%08" PRIx32 " ebp=%08"	\
				PRIx32 "\n", f->esi, f->edi, (uint32_t) f->esp, f->ebp);	\
		MYNEWPRINTF (" cs=%04" PRIx16 " ds=%04" PRIx16 " es=%04" PRIx16 " ss=%04" PRIx16	\
				"\n", f->cs, f->ds, f->es, f->ss);				\
	}while(0);

#endif /* threads/interrupt.h */
