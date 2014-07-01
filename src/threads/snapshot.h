#ifndef THREADS_SNAPSHOT_H
//123124324098
#define THREADS_SNAPSHOT_H
#include <stdint.h>
/*
In physical memory looks like(increasing memory order):
  * Kernel Pages
  * User pages
  * Snapshot pages  <-- smem_base
    - Register page
    - Snapshot pages  } smem //Doesn't include one extra page, total is smem+PGSIZE
*/

#define RECORD_MODE 1
#define REPLAY_MODE 2

struct snap_registers  
{
    uint32_t edi;               /* Saved EDI. */
    uint32_t esi;               /* Saved ESI. */
    uint32_t eip;		/* Saved EIP  */
    uint32_t ebp;               /* Saved EBP. */
    uint32_t esp;               /* Saved ESP. */
    uint32_t ebx;               /* Saved EBX. */
    uint32_t edx;               /* Saved EDX. */
    uint32_t ecx;               /* Saved ECX. */
    uint32_t eax;               /* Saved EAX. */
    uint32_t eflags;            /* Saved CPU flags. */
    uint16_t cs, :16;           /* Code segment for eip. */
    uint16_t ds, :16;           /* Saved DS segment register. */
    uint16_t es, :16;           /* Saved ES segment register. */
    uint16_t fs, :16;           /* Saved FS segment register. */
    uint16_t gs, :16;           /* Saved GS segment register. */
    uint16_t ss, :16;           /* Data segment for esp. */
};

struct snap_frame
{
    /* Use this struct to store information about the memory
       dump that you take so that its easy to restore */
    uint32_t smem;			/* Snapshot memory */
    uint8_t *mem_start;			/* Start of actual memory in snapshot space */
    uint8_t *snapshot_begin;		/* Start of memory snapshot memory */
	uint8_t *record_in_begin;
    uint8_t *record_begin;		/* Start of record entries */
    uint32_t chunks;                    /* Since memory copied is discontinuos, lets store number of total chunks #2 */ 
    uint8_t *chunk_info[20];            /* It stores {Starting address, ending address} of chunk, MAX 10 chunks */
    struct snap_registers registers;    /* Register state snapshot */
    int record_entries;		/* No. of entries in the memory log */
	int record_in_entries;
};

extern int rr_mode;	/* Record/Replay mode, default is NULL */
extern int smem;	/* Memory reserved for snapshot memory */
extern int log_entries;	/* No. of log entries */
extern int log_entries_run;	/* No. of log entries */
extern int in_entries;	/* No. of log entries */
extern int in_entries_run;	/* No. of log entries */
extern uint8_t *smem_base;	/* Starting Address for Memory reserved for snapshot memory */
extern uint8_t *in_ebase;
extern uint8_t *log_ebase;	/* Snapshot memory base for log entries*/
extern int in_entries;
extern int in_entries_run;

void record_init(void);
void take_snapshot(void);
void revert_snapshot(void);
#endif
