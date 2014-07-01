#ifndef THREADS_PERF_H
//1231243122933
#define THREADS_PERF_H

#include "threads/interrupt.h"

#define RetiredBranchInstructions     0xC4
#define ARCH_PERFMON_EVENTSEL_USR     (1ULL << 16)
#define ARCH_PERFMON_EVENTSEL_OS      (1ULL << 17)
#define ARCH_PERFMON_EVENTSEL_INT     (1ULL << 20)
#define ARCH_PERFMON_EVENTSEL_ENABLE  (1ULL << 22)
#define MSR_P6_PERFCTR0               0x000000C1
#define MSR_P6_EVNTSEL0               0x00000186
#define MSR_CORE_PERF_GLOBAL_STATUS   0x0000038E
#define MSR_CORE_PERF_GLOBAL_CTRL     0x0000038F
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL 0x00000390

#define IA32_APIC_BASE 		0x1B
#define APIC_LVTPC         	0x340
#define APIC_EOI			0x00B0
#define APIC_LVT_MASKED    	(1 << 16)
#define APIC_BASE       	0xFEE00000
#define PMI_VECTOR_NO 		0xEC
#define LAPIC_ENABLE_BIT	0x800
#define COUNTER_WIDTH		40
#define COUNTER_MAX			((1ULL << COUNTER_WIDTH)-1)

/* Reads the value of msr in variable low and high */
#define READ_MSR(msr, low, high) 		\
	asm volatile(				\
	  "push %%eax \n\t"			\
	  "push %%edx \n\t"			\
	  "push %%ecx \n\t"			\
	  "mov %2, %%ecx \n\t"			\
	  "rdmsr \n\t"				\
	  "mov %%eax, %0  \n\t"			\
	  "mov %%edx, %1  \n\t"			\
	  "pop %%ecx \n\t"			\
	  "pop %%edx \n\t"			\
	  "pop %%eax \n\t"			\
	  :"=r"(low),"=r"(high):"r"(msr)	\
	  :"%ecx","%eax","%edx"			\
    )

/* Writes the value of low and high into msr */
#define WRITE_MSR(msr, low, high) 		\
	asm(					\
	  "push %%eax \n\t"			\
	  "push %%edx \n\t"			\
	  "push %%ecx \n\t"			\
	  "mov %0, %%eax \n\t"			\
	  "mov %1, %%edx \n\t"			\
	  "mov %2, %%ecx \n\t"			\
	  "wrmsr \n\t"				\
	  "pop %%ecx \n\t"			\
	  "pop %%edx \n\t"			\
	  "pop %%eax \n\t"			\
	  ::"r"(low),"r"(high),"r"(msr) 	\
	  :"%ecx","%eax","%edx"			\
    )

/* Read the current branch count into 'count'(unsigned long long) */
#define READ_BRANCHES(count)							\
	READ_MSR(MSR_P6_PERFCTR0, __temp_low, __temp_high);		\
	count = ((long long int)__temp_high)<<32 | __temp_low;	\

/* Write the branch count 'count'(unsigned long long) */
#define WRITE_BRANCHES(count)								\
	__tcount = count;	\
	__temp_low = __tcount & 0xffffffff;			\
	__temp_high = (unsigned long long int)((__tcount >> 32) & ((1 << 8) - 1));	\
	READ_MSR(MSR_CORE_PERF_GLOBAL_CTRL, __glow, __ghigh);		\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);			\
	WRITE_MSR(MSR_P6_PERFCTR0, __temp_low, __temp_high);		\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, __glow, __ghigh);

/* Enable Performance counters */
#define ENABLE_COUNTERS	\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 1, 0)
		
#define ENABLE_COUNTERS1(glow, ghigh)							\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, glow, ghigh)

/* Disable Performance counters */
#define DISABLE_COUNTERS	\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0)

#define DISABLE_COUNTERS1(glow, ghigh)	\
	READ_MSR(MSR_CORE_PERF_GLOBAL_CTRL, glow, ghigh);		\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0)

#define IS_COUNTER_ENABLED(enabled) \
	READ_MSR(MSR_CORE_PERF_GLOBAL_CTRL, enabled, __temp);

#define INCREMENT_COUNTER(value) \
	READ_BRANCHES(__count);		\
	__count = __count+(value);		\
	WRITE_BRANCHES(__count);

/* Some registers read/write */
#define READ_ECX(old_ecx)			\
	asm volatile("push %%ecx \n\t"		\
		     "pop %0 \n\t"		\
		     :"=r"(old_ecx)::"%ecx")

#define WRITE_ECX(ecx)				\
	asm volatile("push %0 \n\t"		\
		     "pop %%ecx \n\t"		\
		     ::"r"(ecx):"%ecx")

#define MYNEWPRINTF_VARS	\
	uint32_t __flags;			\
	enum intr_level __level;		\
	unsigned int __temp=0;			\
	bool __context;				\
	uint32_t __cr2=0;				\
	int __glow=0, __ghigh=0;		\
	unsigned int __old_ecx=0;			\
	long long int __count=0, __tcount=0;	\
	int __temp_low=0, __temp_high=0;


#define MYNEWPRINTF_MAIN(FUNC, args...)				\
	READ_MSR(MSR_CORE_PERF_GLOBAL_CTRL, __glow, __ghigh);		\
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);			\
	asm volatile ("pushfl; popl %0":"=g" (__flags));			\
	__level = (((__flags&FLAG_IF)>>9)&INTR_ON) | ((!((__flags&FLAG_IF)>>9))&INTR_OFF);\
	asm volatile ("cli":::"memory");				\
	asm volatile("push %%ecx \n\t"					\
		     "pop %0 \n\t"					\
		     :"=r"(__old_ecx)::"%ecx");				\
	FUNC(args);							\
	asm volatile("push %0 \n\t"					\
		     "pop %%ecx \n\t"					\
		     ::"r"(__old_ecx):"%ecx");				\
	if(__level == INTR_ON)						\
	{								\
		ASSERT (!intr_context());				\
		READ_MSR(MSR_P6_PERFCTR0, __temp_low, __temp_high);		\
		__count = ((long long int)__temp_high)<<32 | __temp_low;	\
		/* To accomocate the jmp instruction */			\
		if(__glow) __count = __count - 1;					\
		__temp_low = __count & 0xffffffff;				\
		__temp_high = (long long int)__count>>32;		\
		WRITE_MSR(MSR_P6_PERFCTR0, __temp_low, __temp_high);	\
		WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, __glow, __ghigh);	\
		asm volatile ("sti");					\
	}								\
	else								\
	{								\
		asm volatile ("cli":::"memory");			\
		WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, __glow, __ghigh);	\
	};

#ifdef RRDEBUG
#define MYNEWPRINTF(args...)	\
			MYNEWPRINTF_MAIN(myprintf, args);
#else
#define MYNEWPRINTF(args...) 
#endif


#define MYNEWPRINTF_ERROR(args...)	\
			MYNEWPRINTF_MAIN(myprintf_error, args);

#define GET_DEBUG_FLAG(flag)\
	asm volatile(			\
	  "pushfl \n\t"			\
	  "movl (%%esp), %0 \n\t"	\
	  "popfl \n\t"			\
	  :"=r"(flag)			\
    )

#define SET_DEBUG_FLAG(flag)\
	asm volatile(			\
	  "pushfl \n\t"			\
	  "orl %0, (%%esp) \n\t"	\
	  "popfl \n\t"			\
	  ::"r"(flag)			\
    )

#define ENABLE_DEBUG		\
	asm volatile(			\
	  "pushfl \n\t"			\
	  "orl %0, (%%esp) \n\t"	\
	  "popfl \n\t"			\
	  ::"r"(FLAG_DB):		\
    )

#define DISABLE_DEBUG			\
	asm volatile(			\
	  "pushfl \n\t"			\
	  "andl %0, (%%esp) \n\t"	\
	  "popfl \n\t"			\
	  ::"r"(~FLAG_DB):		\
    )

#define PRINT_COUNTERS_KVM \
	asm volatile ("pushl %%ecx \n\t" 	\
				  "pushl %%eax \n\t"	\
				  "pushl %%edx \n\t"	\
				  "movl $0xC1, %%ecx \n\t"	\
				  "rdmsr \n\t"	\
				  "out %%eax, $0x51 \n\t"	\
				  "popl %%edx \n\t"		\
				  "popl %%eax \n\t"		\
				  "popl %%ecx\n\t" :: );

/* These functions need not be implemented as macros, as the programmer should reset the counter values after calling these functions */
void perf_init(void);
void perf_set_pmi(unsigned int state);
void lapic_init(void);
void perf_info(void);
char *get_msr_name(int msr);
void cpuid(uint32_t val, uint32_t *a,uint32_t *b,uint32_t *c, uint32_t *d);
enum cntr_level cntr_get_level(void);

enum cntr_level {
	CNTR_OFF,
	CNTR_ON
};

#endif
