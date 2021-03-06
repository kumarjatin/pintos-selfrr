#include "threads/interrupt.h"
//89071147255670680
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "threads/perf.h"
#include "threads/record.h"
#include "threads/perf.h"

/* Programmable Interrupt Controller (PIC) registers.
   A PC has two PICs, called the master and slave PICs, with the
   slave attached ("cascaded") to the master IRQ line 2. */
#define PIC0_CTRL	0x20	/* Master PIC control register address. */
#define PIC0_DATA	0x21	/* Master PIC data register address. */
#define PIC1_CTRL	0xa0	/* Slave PIC control register address. */
#define PIC1_DATA	0xa1	/* Slave PIC data register address. */
#define IRQ_CASCADE0   2
#define IRQ_CASCADE1   9
#define TRACE_FLAG 	0x100	/* Trace flag in CPU EFLAGS register */

/* Number of x86 interrupts. */
#define INTR_CNT 256
#define BRANCH_INAC 50

/* The Interrupt Descriptor Table (IDT).  The format is fixed by
   the CPU.  See [IA32-v3a] sections 5.10 "Interrupt Descriptor
   Table (IDT)", 5.11 "IDT Descriptors", 5.12.1.2 "Flag Usage By
   Exception- or Interrupt-Handler Procedure". */
static uint64_t idt[INTR_CNT];

/* Interrupt handler functions for each interrupt. */
static intr_handler_func *intr_handlers[INTR_CNT];

/* Names for each interrupt, for debugging purposes. */
static const char *intr_names[INTR_CNT];

/* cached values for PIC */
static uint8_t pic_mask[2];

/* External interrupts are those generated by devices outside the
   CPU, such as the timer.  External interrupts run with
   interrupts turned off, so they never nest, nor are they ever
   pre-empted.  Handlers for external interrupts also may not
   sleep, although they may invoke intr_yield_on_return() to
   request that a new process be scheduled just before the
   interrupt returns. */
static bool in_external_intr;	/* Are we processing an external interrupt? */
static bool yield_on_return;	/* Should we yield on interrupt return? */

/* Programmable Interrupt Controller helpers. */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/* Interrupt Descriptor Table helpers. */
static uint64_t make_intr_gate (void (*)(void), int dpl);
static uint64_t make_trap_gate (void (*)(void), int dpl);
static inline uint64_t make_idtr_operand (uint16_t limit, void *base);

typedef unsigned long long ULL;

static int setup_done;
uint32_t intr_count;
uint8_t *smem_base;
uint8_t *log_ebase;
uint8_t *in_ebase;
static int interrupts_received=0;
long long int look_for_branches;
static bool branch_adjusted=false;
int pmi_expected=0;

/* Interrupt handlers. */
void intr_handler (struct intr_frame *args);

/* Returns the current interrupt status. */
	enum intr_level
intr_get_level (void)
{
	uint32_t flags;

	/* Push the flags register on the processor stack, then pop the
	   value off the stack into `flags'.  See [IA32-v2b] "PUSHF"
	   and "POP" and [IA32-v3a] 5.8.1 "Masking Maskable Hardware
	   Interrupts". */
	asm volatile ("pushfl; popl %0":"=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* Enables or disables interrupts as specified by LEVEL and
   returns the previous interrupt status. */
	enum intr_level
intr_set_level (enum intr_level level)
{
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/* Enables interrupts and returns the previous interrupt status. */
	enum intr_level
intr_enable (void)
{
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	/* Enable interrupts by setting the interrupt flag.

	   See [IA32-v2b] "STI" and [IA32-v3a] 5.8.1 "Masking Maskable
	   Hardware Interrupts". */
	asm volatile ("sti");

	return old_level;
}

/* Disables interrupts and returns the previous interrupt status. */
	enum intr_level
intr_disable (void)
{
	enum intr_level old_level = intr_get_level ();

	/* Disable interrupts by clearing the interrupt flag.
	   See [IA32-v2b] "CLI" and [IA32-v3a] 5.8.1 "Masking Maskable
	   Hardware Interrupts". */
	asm volatile ("cli":::"memory");

	return old_level;
}

/* Initializes the interrupt system. */
	void
intr_init (void)
{
	uint64_t idtr_operand;
	int i;

	/* Initialize interrupt controller. */
	pic_init ();

	/* Initialize IDT. */
	for (i = 0; i < INTR_CNT; i++)
		idt[i] = make_intr_gate (intr_stubs[i], 0);

	/* Load IDT register.
	   See [IA32-v2a] "LIDT" and [IA32-v3a] 5.10 "Interrupt
	   Descriptor Table (IDT)". */
	idtr_operand = make_idtr_operand (sizeof idt - 1, idt);
	asm volatile ("lidt %0"::"m" (idtr_operand));

	/* Initialize intr_names. */
	for (i = 0; i < INTR_CNT; i++)
		intr_names[i] = "unknown";
	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/* Registers interrupt VEC_NO to invoke HANDLER with descriptor
   privilege level DPL.  Names the interrupt NAME for debugging
   purposes.  The interrupt handler will be invoked with
   interrupt status set to LEVEL. */
	static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func * handler, const char *name)
{
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON)
		idt[vec_no] = make_trap_gate (intr_stubs[vec_no], dpl);
	else
		idt[vec_no] = make_intr_gate (intr_stubs[vec_no], dpl);
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}

/* Registers external interrupt VEC_NO to invoke HANDLER, which
   is named NAME for debugging purposes.  The handler will
   execute with interrupts disabled. */
	void
intr_register_ext (uint8_t vec_no, intr_handler_func * handler,
		const char *name)
{
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/* Registers internal interrupt VEC_NO to invoke HANDLER, which
   is named NAME for debugging purposes.  The interrupt handler
   will be invoked with interrupt status LEVEL.

   The handler will have descriptor privilege level DPL, meaning
   that it can be invoked intentionally when the processor is in
   the DPL or lower-numbered ring.  In practice, DPL==3 allows
   user mode to invoke the interrupts and DPL==0 prevents such
   invocation.  Faults and exceptions that occur in user mode
   still cause interrupts with DPL==0 to be invoked.  See
   [IA32-v3a] sections 4.5 "Privilege Levels" and 4.8.1.1
   "Accessing Nonconforming Code Segments" for further
   discussion. */
	void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func * handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/* Returns true during processing of an external interrupt
   and false at all other times. */
	bool
intr_context (void)
{
	return in_external_intr;
}

/* During processing of an external interrupt, directs the
   interrupt handler to yield to a new process just before
   returning from the interrupt.  May not be called at any other
   time. */
	void
intr_yield_on_return (void)
{
	ASSERT (intr_context ());
	yield_on_return = true;
}

/* 8259A Programmable Interrupt Controller. */

/* Initializes the PICs.  Refer to [8259A] for details.

   By default, interrupts 0...15 delivered by the PICs will go to
   interrupt vectors 0...15.  Those vectors are also used for CPU
   traps and exceptions, so we reprogram the PICs so that
   interrupts 0...15 are delivered to interrupt vectors 32...47
   (0x20...0x2f) instead. */
	static void
pic_init (void)
{
	/* Mask all interrupts on both PICs. */
	outb (PIC0_DATA, 0xff);
	outb (PIC1_DATA, 0xff);

	/* Initialize master. */
	outb (PIC0_CTRL, 0x11);	/* ICW1: single mode, edge triggered, expect ICW4. */
	outb (PIC0_DATA, 0x20);	/* ICW2: line IR0...7 -> irq 0x20...0x27. */
	outb (PIC0_DATA, 0x04);	/* ICW3: slave PIC on line IR2. */
	outb (PIC0_DATA, 0x01);	/* ICW4: 8086 mode, normal EOI, non-buffered. */

	/* Initialize slave. */
	outb (PIC1_CTRL, 0x11);	/* ICW1: single mode, edge triggered, expect ICW4. */
	outb (PIC1_DATA, 0x28);	/* ICW2: line IR0...7 -> irq 0x28...0x2f. */
	outb (PIC1_DATA, 0x02);	/* ICW3: slave ID is 2. */
	outb (PIC1_DATA, 0x01);	/* ICW4: 8086 mode, normal EOI, non-buffered. */

	/* Unmask all interrupts. */
	outb (PIC0_DATA, 0x00);
	outb (PIC1_DATA, 0x00);
	pic_mask[0] = 0;
	pic_mask[1] = 0;
}

/* Sends an end-of-interrupt signal to the PIC for the given IRQ.
   If we don't acknowledge the IRQ, it will never be delivered to
   us again, so this is important.  */
	static void
pic_end_of_interrupt (int irq)
{
	ASSERT (irq >= 0x20 && irq < 0x30);

	/* Acknowledge master PIC. */
	outb (0x20, 0x20);

	/* Acknowledge slave PIC if this is a slave interrupt. */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}

/* Creates an gate that invokes FUNCTION.

   The gate has descriptor privilege level DPL, meaning that it
   can be invoked intentionally when the processor is in the DPL
   or lower-numbered ring.  In practice, DPL==3 allows user mode
   to call into the gate and DPL==0 prevents such calls.  Faults
   and exceptions that occur in user mode still cause gates with
   DPL==0 to be invoked.  See [IA32-v3a] sections 4.5 "Privilege
   Levels" and 4.8.1.1 "Accessing Nonconforming Code Segments"
   for further discussion.

   TYPE must be either 14 (for an interrupt gate) or 15 (for a
   trap gate).  The difference is that entering an interrupt gate
   disables interrupts, but entering a trap gate does not.  See
   [IA32-v3a] section 5.12.1.2 "Flag Usage By Exception- or
   Interrupt-Handler Procedure" for discussion. */
	static uint64_t
make_gate (void (*function) (void), int dpl, int type)
{
	uint32_t e0, e1;

	ASSERT (function != NULL);
	ASSERT (dpl >= 0 && dpl <= 3);
	ASSERT (type >= 0 && type <= 15);

	e0 = (((uint32_t) function & 0xffff)	/* Offset 15:0. */
			| (SEL_KCSEG << 16));	/* Target code segment. */

	e1 = (((uint32_t) function & 0xffff0000)	/* Offset 31:16. */
			| (1 << 15)		/* Present. */
			| ((uint32_t) dpl << 13)	/* Descriptor privilege level. */
			| (0 << 12)		/* System. */
			| ((uint32_t) type << 8));	/* Gate type. */

	return e0 | ((uint64_t) e1 << 32);
}

/* Creates an interrupt gate that invokes FUNCTION with the given
   DPL. */
	static uint64_t
make_intr_gate (void (*function) (void), int dpl)
{
	return make_gate (function, dpl, 14);
}

/* Creates a trap gate that invokes FUNCTION with the given
   DPL. */
	static uint64_t
make_trap_gate (void (*function) (void), int dpl)
{
	return make_gate (function, dpl, 15);
}

/* Returns a descriptor that yields the given LIMIT and BASE when
   used as an operand for the LIDT instruction. */
	static inline uint64_t
make_idtr_operand (uint16_t limit, void *base)
{
	return limit | ((uint64_t) (uint32_t) base << 16);
}

/* Setup branch count for next PMI or directly single step
   - Sets up the required branch count in counter and the global
     variable look_for_branches
*/
int intr_setup_next_replay(int set_db)
{
	//Interrupts and counter should be disabled
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (cntr_get_level () == CNTR_OFF);
	
	struct record_loop *rl = (struct record_loop *) (log_ebase + log_entries_run*sizeof (struct record_loop));
	ULL last_branches, diff;

	MYNEWPRINTF_VARS;
	if(branch_adjusted)
	{
		INCREMENT_COUNTER((rl-1)->branches - 1000 - BRANCH_INAC);
		branch_adjusted = false;
	}
	if(log_entries_run == log_entries) return 0;
	ASSERT(log_entries_run == rl->extra);

	READ_BRANCHES(last_branches);
	diff = rl->branches - last_branches;

	if((long long int)diff <= 0)
	{
		MYNEWPRINTF_ERROR("Diff in %s is negative = %lld, rl=%d, rl->branches=%lld, last_branches=%lld\n", __func__, diff, log_entries_run, rl->branches, last_branches);
		PANIC("Negative diff bug\n");
	}
	if(diff>140)
	{
		//Set up PMI
		MYNEWPRINTF("In %s, <Installing PMI> setting branches = -%lld, loop_count=%d, vec=%d\n", __func__, diff-BRANCH_INAC, loop_counter, rl->vec_no);
		WRITE_BRANCHES(-diff+BRANCH_INAC);
		perf_set_pmi(1);
		READ_BRANCHES(diff);
		MYNEWPRINTF("After setting up PMI, branches set to %lld\n", diff);
		look_for_branches = BRANCH_INAC+1000;
		branch_adjusted = true;
		return 0;
	}
	else
	{
		//Set up Single Stepping
		MYNEWPRINTF("In %s, <Single Setpping> setting branches = %lld\n", __func__, last_branches);
		look_for_branches = rl->branches;
		if(set_db) ENABLE_DEBUG;
		return 1;
	}
}

void intr_replay_interrupt(struct intr_frame *frame, int cntr_state)
{
	ASSERT (cntr_get_level () == CNTR_OFF);
	/* It assumes that the branche count is readily set to play the interrupt */
	MYNEWPRINTF_VARS;
	ULL branches;

	/******************/
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (!intr_context ());

	in_external_intr = true;
	yield_on_return = false;

	intr_handler_func *handler = intr_handlers[frame->vec_no];

	if (handler != NULL) 
	{
		READ_BRANCHES(branches);
		MYNEWPRINTF("Calling handler %p, branches=%lld\n", handler, branches);
		ENABLE_COUNTERS1(cntr_state, 0);
		handler (frame);
		DISABLE_COUNTERS;
		READ_BRANCHES(branches);
		MYNEWPRINTF("After calling handler, branches=%lld\n", branches);

	}
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f)
	{
		MYNEWPRINTF("Useless recorded interrupt\n");
	}
	else
	{
		/* Invoke the unexpected interrupt handler. */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (intr_context ());

	in_external_intr = false;
	ASSERT (intr_get_level () == INTR_OFF);
}

/* Interrupt handlers. */
void intr_pmi_handler(struct intr_frame *frame)
{
	PANIC("I should never come here\n");
}

void intr_debug_handler_out(struct intr_frame *frame)
{
	printf("%s: EIP=%p, ECX=%x\n", __func__, frame->eip, frame->ecx);
}

void intr_debug_handler(struct intr_frame *frame)
{
	long long int branches;
	int cntr_state;
	MYNEWPRINTF_VARS;
	IS_COUNTER_ENABLED(cntr_state);
	DISABLE_COUNTERS;

	//MYNEWPRINTF("Entered here vec_no=%d at EIP=%p, branches=%lld, intr_enter_branches=%lld, tintr=%d\n", frame->vec_no, frame->eip, branches, intr_enter_branches, interrupts_received);
 	branches = intr_enter_branches;
	ASSERT (intr_get_level() == INTR_OFF);

	if(frame->vec_no == PMI_VECTOR_NO)
	{
		/* Adjust branch count by incrementing it by 1000 */
		if(!pmi_expected)
		{
			MYNEWPRINTF("Bogus PMI Interrupt. Its not expected here\n");
			PANIC("Bogus PMI bug");
		}
		INCREMENT_COUNTER(1000);
		intr_enter_branches += 1000;
		perf_set_pmi(0);
		frame->eflags |= FLAG_DB;
	}

	branches = (intr_enter_branches + 5) & COUNTER_MAX;
	WRITE_BRANCHES(branches);

	struct record_loop *rl;
	bool replayed_intr = false;
	bool instruction_skipped;
	bool force_inject = false;

replay_checks:
	instruction_skipped = false;
	replayed_intr = false;
	force_inject = false;
	READ_BRANCHES(branches);

	/* Replay all the interrupts that can be */
	rl = (struct record_loop *) (log_ebase + log_entries_run*sizeof (struct record_loop));

	/* Counters should always be enabled in the actual code OR
	 * I should not be stepping ever my record/replay code */
	if(cntr_state == 0)
	{
		MYNEWPRINTF_ERROR("Counters are disabled in the %s function. EIP = %p\n", __func__, frame->eip);
		MYNEWPRINTF_ERROR("Bug (%p, %08x, %lld)->(%p, %08x, %lld)\n", frame->eip, frame->ecx, branches, rl->frame.eip, rl->frame.ecx, look_for_branches);
		my_debug_backtrace();
		PANIC("Its a disabled counter bug");
	}

	/* I should not be single stepping a WRMSR instruction ever */
	if(*((unsigned char *)frame->eip)==0x0F && *((unsigned char *)frame->eip+1)==0x30)
	{
		//TODO: Why doesn't it halt immediately?
		MYNEWPRINTF_ERROR("Buggy program. Single stepping WRMSR instruction at %p\n", frame->eip);
		MYNEWPRINTF_ERROR("Bug (%p, %08x, %lld)->(%p, %08x, %lld)\n", frame->eip, frame->ecx, branches, rl->frame.eip, rl->frame.ecx, look_for_branches);
		my_debug_backtrace();
		PANIC("It's a WRMSR bug");
	}

	/* Current branch count should never be more than what I am looking for */
	if(branches > look_for_branches + 5)
	{
		MYNEWPRINTF_ERROR("Replay bug. Branch count overshoot\n");
		MYNEWPRINTF_ERROR("Bug (%p, %08x, %lld)->(%p, %08x, %lld)\n", frame->eip, frame->ecx, branches, rl->frame.eip, rl->frame.ecx, look_for_branches);
		my_debug_backtrace();
		PANIC("It's a replay bug");
	}
	
	/* Instruction skip bug due to some QEMU limitations */
	if( ( frame->ecx == rl->frame.ecx) && (frame->eip > rl->frame.eip)
	   && ((look_for_branches == branches) || (look_for_branches+1 == branches)))
	{
		MYNEWPRINTF_ERROR("Instruction skip bug. Trying to recover\n");
		force_inject=true;
	}

	/* 1. There was some issue of the instruction immediately after out (EE) being skipped and it creates
	 *    problem when the record log has that particular as one of the entries. So I check the
	 *    next instruction for being a leave instruction and then inject the interrupt.
	 *
	 * 2. We DON'T get Debug Trap after executing INT instruction. So we need to look ahead and
	 *    inject before executing INT instruction.
	 *
	 * 3. Due to some other issues with QEMU/KVM a replay point can be skipped due to not getting an
	 *    interrupt, so we try to recover with `force_inject`.
	 *
	 * NOTE: The EIP in the debug trap frame is of instruction which would be executed next.
	 *
	 */
	while ( ( (frame->eip == rl->frame.eip) || force_inject
		   || (*((unsigned char *)frame->eip)==0xCD && frame->eip+2==rl->frame.eip) // If desired EIP is next to INT instruction
		   || (*((unsigned char *)frame->eip)==0xEE && frame->eip+1==rl->frame.eip) // If desired EIP is next to OUT instruction
		 ) && (frame->ecx == rl->frame.ecx) && (look_for_branches == branches) )
	{
		force_inject = false;
		/* Replay current interrupt */
		frame->eflags &= ~FLAG_DB;
		ASSERT (intr_get_level() == INTR_OFF);
		MYNEWPRINTF("Branches before injecting interrupt %d are %lld with tid=%d\n", log_entries_run, branches, thread_tid());
		intr_replay_interrupt((struct intr_frame *)&(rl->frame), cntr_state);
		log_entries_run++;

		ASSERT (intr_get_level() == INTR_OFF);
		READ_BRANCHES(branches);
		MYNEWPRINTF("Replaying interrupt %d, INTR_LEVEL=%d, branches=%lld with tid=%d\n", log_entries_run, intr_get_level(), branches, thread_tid());

		/* Prepare for next interrupt */
		if(intr_setup_next_replay(0)) frame->eflags |= FLAG_DB;

		if (yield_on_return)
		{
			MYNEWPRINTF("Will possibly context switch in %s, Runnables=%d\n", __func__, thread_runnables());
			set_single_stepping = frame->eflags & FLAG_DB;
			ENABLE_COUNTERS;
			/* Enable single stepping so that if it context switches, we don't miss the replay point */
			SET_DEBUG_FLAG((frame->eflags & FLAG_DB));
			thread_yield ();
			DISABLE_COUNTERS;
			rl = (struct record_loop *) (log_ebase + log_entries_run*sizeof (struct record_loop));
			
			READ_BRANCHES(branches);
			if(((signed long long)branches > -15) && ((signed long long)branches <0) && pmi_expected)
			{
				// Not good to continue in PMI mode, switch to single stepping mode
				// Otherwise the branch adjustments will lead to bogus PMI
				perf_set_pmi(0);
				INCREMENT_COUNTER(rl->branches - BRANCH_INAC);
				branch_adjusted = false;
				MYNEWPRINTF("Specially adjusted the branch count = %lld\n", branches);
				look_for_branches = rl->branches;
				frame->eflags |= FLAG_DB;
			}
		}
		rl = (struct record_loop *) (log_ebase + log_entries_run*sizeof (struct record_loop));

		INCREMENT_COUNTER(9);	//branches increment by 9 if immediately followed by another interrupt
		READ_BRANCHES(branches);
		replayed_intr =  true;
	}

	if(replayed_intr){
		INCREMENT_COUNTER(-11);
	}
	else
	{
		MYNEWPRINTF("(%p, %08x, %lld)->(%p, %08x, %lld)\n", frame->eip, frame->ecx, branches, rl->frame.eip, rl->frame.ecx, look_for_branches);
		if(*((unsigned char *)frame->eip)==0xFB)
		{
			// Simulate next STI instruction
			MYNEWPRINTF("Skipping next STI instruction at EIP %p\n", frame->eip);
			frame->eip = ((unsigned char *)(frame->eip) + 1);
			frame->eflags |= FLAG_IF;
			instruction_skipped = true;
		}

		if(*((unsigned char *)frame->eip)==0xF4)
		{
			// Skip next HLT instruction
			MYNEWPRINTF("Skipping next HLT instruction at EIP %p\n", frame->eip);
			frame->eip = ((unsigned char *)(frame->eip) + 1);
			instruction_skipped = true;
		}

		if (instruction_skipped) goto replay_checks;

		WRITE_BRANCHES(intr_enter_branches-6);
		//MYNEWPRINTF("Branches set to %lld\n", branches);
	}

	//MYNEWPRINTF("Exiting %s function with branches=%lld\n", __func__, branches);
	set_single_stepping = frame->eflags & FLAG_DB;
	ENABLE_COUNTERS;
	return;
}

/* Handler for all interrupts, faults, and exceptions.  This
   function is called by the assembly language interrupt stubs in
   intr-stubs.S.  FRAME describes the interrupt and the
   interrupted thread's registers. */
void
intr_handler (struct intr_frame *frame)
{
	bool external;
	intr_handler_func *handler;
	long long int branches;
	int cntr_state;
	MYNEWPRINTF_VARS;

	interrupts_received++;
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
	if(!external)
	{
		/* Handle internal interrupt */
		handler = intr_handlers[frame->vec_no];
		if (handler != NULL)
			handler (frame);
		else
		{
			intr_dump_frame (frame);
			PANIC ("Unexpected interrupt");
		}
	}
	else
	{
		/* Handle external interrupt */
		IS_COUNTER_ENABLED(cntr_state);
		DISABLE_COUNTERS;

		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());
		if(enable_replay != 1)
		{
			in_external_intr = true;
			yield_on_return = false;
		}

		if((enable_record || enable_replay) && (cntr_state == 0))
		{
			MYNEWPRINTF_ERROR("Bug: Interrupt with counters disabled during record.");
			debug_backtrace();
		}
		if ((rr_mode == RECORD_MODE) && (enable_record == 1))
		{
			struct record_loop *rl = (struct record_loop *) (log_ebase + log_entries * sizeof (struct record_loop));
			READ_BRANCHES (branches);
			rl->branches = branches;
			rl->vec_no = frame->vec_no;
			memcpy ((void *) &(rl->frame), (void *) frame, sizeof (struct intr_frame));
			MYNEWPRINTF("Recording %x at intr_enter_branches=%lld, branches=%lld\n", frame->vec_no, intr_enter_branches, branches);
			MYNEWPRINTF("Interrupt %#04x (%s) at eip=%p, log_entries=%d\n", frame->vec_no, intr_names[frame->vec_no], frame->eip, log_entries+1);
			//my_debug_backtrace();
			rl->counter = loop_counter;
			rl->extra = log_entries;
			log_entries++;
		}
		else if((rr_mode == REPLAY_MODE) && (enable_replay == 1))
		{
			WRITE_BRANCHES(intr_enter_branches - 4);
			goto ext_end;
		} 

		handler = intr_handlers[frame->vec_no];
		if (handler != NULL)
		{
			READ_BRANCHES(branches);
			if ((rr_mode == RECORD_MODE) && (enable_record == 1))
			{
				MYNEWPRINTF("Before calling handler, branches=%lld\n", branches);
			}
			ENABLE_COUNTERS1(cntr_state,0);
			handler (frame);
			DISABLE_COUNTERS;
			READ_BRANCHES(branches);
			if ((rr_mode == RECORD_MODE) && (enable_record == 1))
			{
				MYNEWPRINTF("After calling handler, branches=%lld\n", branches);
			}
		}
		else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {}
		else
		{
			intr_dump_frame (frame);
			PANIC ("Unexpected interrupt");
		}

		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());
	ext_end:	

		if(enable_replay != 1)
		{
			in_external_intr = false;
		}

		pic_end_of_interrupt (frame->vec_no);

		if (yield_on_return && (enable_replay!=1))
		{
			MYNEWPRINTF("Will possibly context switch in %s, Runnables=%d\n", __func__, thread_runnables());
			ENABLE_COUNTERS;
			thread_yield ();
			DISABLE_COUNTERS;
		}
		if ((rr_mode == RECORD_MODE) && (enable_record == 1))
		{
			READ_BRANCHES (branches);
			MYNEWPRINTF("Exiting intr_handler, branches = %lld\n", branches);
		}
		if ((rr_mode == REPLAY_MODE) && (enable_replay == 1))
		{
			READ_BRANCHES (branches);
			MYNEWPRINTF("Interrupt in Replay mode with VEC=%x, EIP=%p, branches=%lld\n", frame->vec_no, frame->eip, branches);
		}
		ENABLE_COUNTERS1(cntr_state, 0);
	}
	//MYNEWPRINTF("Exiting for intr_vec_no = %d, cntr_state=%d, branches=%lld\n", frame->vec_no, cntr_state, branches);
	return;
}

/* Dumps interrupt frame F to the console, for debugging. */
	void
intr_dump_frame (const struct intr_frame *f)
{
	uint32_t cr2;

	/* Store current value of CR2 into `cr2'.
	   CR2 is the linear address of the last page fault.
	   See [IA32-v2a] "MOV--Move to/from Control Registers" and
	   [IA32-v3a] 5.14 "Interrupt 14--Page Fault Exception
	   (#PF)". */
	asm ("movl %%cr2, %0":"=r" (cr2));

	printf ("Interrupt %#04x (%s) at eip=%p\n",
			f->vec_no, intr_names[f->vec_no], f->eip);
	printf (" cr2=%08" PRIx32 " error=%08" PRIx32 "\n", cr2, f->error_code);
	printf (" eax=%08" PRIx32 " ebx=%08" PRIx32 " ecx=%08" PRIx32 " edx=%08"
			PRIx32 "\n", f->eax, f->ebx, f->ecx, f->edx);
	printf (" esi=%08" PRIx32 " edi=%08" PRIx32 " esp=%08" PRIx32 " ebp=%08"
			PRIx32 "\n", f->esi, f->edi, (uint32_t) f->esp, f->ebp);
	printf (" cs=%04" PRIx16 " ds=%04" PRIx16 " es=%04" PRIx16 " ss=%04" PRIx16
			"\n", f->cs, f->ds, f->es, f->ss);
}

/* Returns the name of interrupt VEC. */
	const char *
intr_name (uint8_t vec)
{
	return intr_names[vec];
}

/** masks a given IRQ */
	void
intr_irq_mask (int irq)
{
	if (irq < 8)
	{
		pic_mask[0] |= 1 << irq;
		outb (PIC0_DATA, pic_mask[0]);
	}
	else
	{
		pic_mask[1] |= 1 << (irq - 8);
		outb (PIC1_DATA, pic_mask[1]);
	}
}

/** unmasks a given IRQ */
	void
intr_irq_unmask (int irq)
{
	if (irq >= 8)
	{
		/* enable cascade if not enabled for pic2 */
		if (pic_mask[1] & (1 << (IRQ_CASCADE1 - 8)))
			pic_mask[1] &= ~(1 << (IRQ_CASCADE1 - 8));

		pic_mask[1] &= ~(1 << (irq - 8));
		outb (PIC1_DATA, pic_mask[1]);

		/* enable cascade if not enabled for pic1 */
		if (pic_mask[0] & (1 << IRQ_CASCADE0))
			irq = IRQ_CASCADE0;
	}

	if (irq < 8)
	{
		pic_mask[0] &= ~(1 << irq);
		outb (PIC0_DATA, pic_mask[0]);
	}

}

/* return whether an interrupt vector is registered */
	bool
intr_is_registered (uint8_t vec_no)
{
	return (intr_handlers[vec_no] != NULL);
}

	void
intr_record_flag (int val)
{
	setup_done = val;
	return;
}
