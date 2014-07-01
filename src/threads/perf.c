#include <stdio.h>
#include "threads/perf.h"
#include "threads/interrupt.h"

/* Setup Performance monitoring counters */
void perf_init(void)
{
    MYNEWPRINTF_VARS;
    WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);
    unsigned int evnt_brcount= RetiredBranchInstructions | ARCH_PERFMON_EVENTSEL_USR \
			       | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE;
    WRITE_MSR(MSR_P6_EVNTSEL0, evnt_brcount, 0);
	WRITE_MSR(MSR_P6_PERFCTR0, 0, 0);
    WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 1, 0);
}

/* Quick function to enable disable PMI */
void perf_set_pmi(unsigned int state)
{
	pmi_expected = state;
	unsigned int low, high;
	unsigned int low1, high1;
	READ_MSR(MSR_CORE_PERF_GLOBAL_CTRL, low1, high1);
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);
	READ_MSR(MSR_P6_EVNTSEL0, low, high);
	WRITE_MSR(MSR_P6_EVNTSEL0, low | (state << 20), high);
	/* Acknowledge any outstanding interrupt */
	*(unsigned int *)(APIC_BASE+APIC_EOI) = 0; 
	WRITE_MSR(MSR_CORE_PERF_GLOBAL_CTRL, low1, high1);
}

/* Configure and enable LAPIC */
void lapic_init(void)
{
    MYNEWPRINTF_VARS;
	/* Check if LAPIC is present else PANIC */
	unsigned int low, high;
	READ_MSR(IA32_APIC_BASE, low, high);
	int ans = low & LAPIC_ENABLE_BIT;
	if(ans==0) PANIC("LAPIC not present");
	*(unsigned int *)(APIC_BASE+APIC_LVTPC) = PMI_VECTOR_NO; 
	myprintf_info("LAPIC Intialized\n");
}

/* Get PMU Information */
void perf_info(void)
{
  uint32_t eax=0,ebx=0,ecx=0,edx=0; 
  cpuid(0xa, &eax, &ebx, &ecx, &edx);
  myprintf_info("\n-----------Architecture PMU Information-----------\n");
  myprintf_info("Performance Monitoring version: %d (> 0 => Supported)\n",eax&0xff);
  myprintf_info("Number of Fixed Function Performance Counters: %d\n",edx&0x1f);
  myprintf_info("Bit width of Counters (Should be %d): %d\n",COUNTER_WIDTH, (edx&0xFD0)>>5);
  myprintf_info("---------------------------------------------------\n");
}

/* Checks if the branch_count increases precisely */
int perf_test(void)
{
    MYNEWPRINTF_VARS;
	int i,branches[16]={0};
	asm volatile(
		"pushl %%ebx \n\t"
		"pushl %%eax \n\t"
		"movl %0, %%eax \n\t"
		"movl %%eax, %%ebx \n\t"
		"addl $0x40, %%eax \n\t"
		"movl %[perfctrl0], %%ecx \n\t"
		"pushl %%eax\n\t"
		"rdmsr \n\t"
		"movl %%eax, (%%ebx) \n\t"
		"popl %%eax \n\t"
		"addl $0x4, %%ebx \n\t"
		"cmp %%eax, %%ebx \n\t"
		"jne . - 0xb \n\t"
		"popl %%eax \n\t"
		"popl %%ebx \n\t"
		::"r"(&branches),
		[perfctrl0]"i"(MSR_P6_PERFCTR0)
	);
	DISABLE_COUNTERS;
	for(i=0;i<16;i++)
		printf("Branches[%d]=%d\n",i,branches[i]);
        return 1;
	ENABLE_COUNTERS;
}

char *get_msr_name(int msr)
{
	switch(msr)
	{
		case 0xc1: return "MSR_P6_PERFCTR0";
		case 0x186: return "MSR_P6_PERFCTR0";
		case 0x38e: return "MSR_CORE_PERF_GLOBAL_STATUS";
		case 0x38f: return "MSR_CORE_PERF_GLOBAL_CTRL";
		case 0x390: return "MSR_CORE_PERF_GLOBAL_OVF_CTRL";
		default: return "UNKNOWN";
	}
}

/* CPUID, 0x1 gives basic information about the processor.
 * CPUID, 0xA is specifically for fetching PMU related information
 * Refer to page 579,582 of Intel's Manual
 */
void cpuid(uint32_t val, uint32_t *a,uint32_t *b,uint32_t *c, uint32_t *d)
{
	asm("mov %4, %%eax \t\n"
	    "cpuid \t\n"
	    "mov %%eax, %0 \t\n"
	    "mov %%ebx, %1 \t\n"
	    "mov %%ecx, %2 \t\n"
	    "mov %%edx, %3 \t\n"
	    :"=r"(*a),"=r"(*b),"=r"(*c),"=r"(*d)
	    :"r"(val)
	);
}

/* Get current counter level */
enum cntr_level cntr_get_level(void)
{
	enum cntr_level level;
    MYNEWPRINTF_VARS;
	IS_COUNTER_ENABLED(level);
	return level;
}
