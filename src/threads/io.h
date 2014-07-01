#ifndef THREADS_IO_H
//3236256
#define THREADS_IO_H

#include <stddef.h>
#include <stdint.h>
#include <debug.h>
#include <stdio.h>
#include "threads/snapshot.h"
#include "threads/interrupt.h"
#include "threads/perf.h"

/* All the in* instructions should be wrapped to record the input
 * in RECORD mode and use the record log when replaying
 */

enum record_type {
	INB,
	INSB,
	INW,
	INSW,
	INL,
	INSL
};

struct record_input {
	enum record_type type;
	uint16_t port;
	size_t size;
	int int_entry_no;
};

/* Reads and returns a byte from PORT. */
static inline uint8_t
inb (uint16_t port)
{
  /* See [IA32-v2a] "IN". */
  uint8_t data;
  bool context;
  uint32_t eflags, flags, old_ecx, glow, ghigh, level;
  uint64_t branches;
  MYNEWPRINTF_VARS; 
  if(enable_record)
  {
	  INTR_GET_LEVEL(level);
	  INTR_DISABLE;
	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
	  struct record_input *frame = (struct record_input*)in_ebase;
	  frame->type = INB;
	  frame->port = port;
	  frame->size = 1;
	  frame->int_entry_no = log_entries;
	  in_ebase += sizeof(struct record_input);
	  *in_ebase = data;
	  in_ebase++;
	  in_entries++;
	  READ_BRANCHES(branches);
	  //myprintf("INB: branches=%lld,%d,%d,%d\n",branches,glow,ghigh,level); 
	  if(glow)
	  {
		  /* Adjust for 1 extra branch on the replay path
		   * coming from one extra check of this if section */
		  INCREMENT_COUNTER(1);
	  }
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER(level, glow, ghigh);
  }
  else if(enable_replay)
  {
	  bool context;
	  uint32_t eflags, flags, old_ecx;
	  MYNEWPRINTF_VARS; 
	  INTR_GET_LEVEL(level);
	  INTR_DISABLE;

	  /* I don't want this piece of code to be single stepped */
	  GET_DEBUG_FLAG(eflags);
	  DISABLE_DEBUG;

	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  struct record_input *frame = (struct record_input*)in_ebase;
	  ASSERT(frame->type == INB);
	  ASSERT(frame->port == port);
	  READ_BRANCHES(branches);
	  MYNEWPRINTF("In INB, port=%d, in_ebase=%p, branches=%lld\n", frame->port, in_ebase, branches);
	  in_ebase += sizeof(struct record_input);
	  data = *in_ebase;
	  in_ebase++;
	  in_entries_run++;
	  READ_BRANCHES(branches);
	  //myprintf("INB: branches=%lld,%d,%d,%d\n",branches,glow,ghigh,level); 
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER_DEBUG(level, glow, ghigh, eflags);
  }
  else
  {
	  asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
  }
  return data;
}

/* Reads CNT bytes from PORT, one after another, and stores them
   into the buffer starting at ADDR. */
static inline void
insb (uint16_t port, void *addr, size_t cnt)
{
  /* See [IA32-v2a] "INS". */
  asm volatile ("rep insb" : "+D" (addr), "+c" (cnt) : "d" (port) : "memory");
}

/* Reads and returns 16 bits from PORT. */
static inline uint16_t
inw (uint16_t port)
{
  uint16_t data;
  /* See [IA32-v2a] "IN". */
  asm volatile ("inw %w1, %w0" : "=a" (data) : "Nd" (port));
  return data;
}

/* Reads CNT 16-bit (halfword) units from PORT, one after
   another, and stores them into the buffer starting at ADDR. */
static inline void
insw (uint16_t port, void *addr, size_t cnt)
{
  /* See [IA32-v2a] "INS". */
  size_t cnt_backup = cnt;
  void *addr_backup = addr;
  uint32_t glow, ghigh, level;
  uint64_t branches;
  if(enable_record)
  {
	  bool context;
	  uint32_t flags, old_ecx;
	  MYNEWPRINTF_VARS; 
	  INTR_GET_LEVEL(level);
	  
	  INTR_DISABLE;
	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  asm volatile ("rep insw" : "+D" (addr), "+c" (cnt) : "d" (port) : "memory");
	  size_t i;
	  struct record_input *frame = (struct record_input*)in_ebase;
	  frame->type = INSW;
	  frame->port = port;
	  cnt = cnt_backup;
	  frame->size = cnt*2;
	  frame->int_entry_no = log_entries;
	  in_ebase += sizeof(struct record_input);
	  myprintf("INSW: Recorded at %p, %d data\n", in_ebase, cnt*2);
	  addr = addr_backup;
	  for(i=0;i<cnt*2;i++) *(in_ebase+i)=*((uint8_t*)addr+i);
	  in_ebase += cnt*2;
	  in_entries++;
	  if(glow)
	  {
		  //Adjust for 1 extra branch on the replay path
		  INCREMENT_COUNTER(1);
	  }
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER(level, glow, ghigh);
  }
  else if(enable_replay)
  {
	  bool context;
	  uint32_t eflags, flags, old_ecx;
	  MYNEWPRINTF_VARS; 
	  INTR_GET_LEVEL(level);
	  INTR_DISABLE;

	  /* I don't want this piece of code to be single stepped */
	  GET_DEBUG_FLAG(eflags);
	  DISABLE_DEBUG;

	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  struct record_input *frame = (struct record_input*)in_ebase;
	  ASSERT(frame->type == INSW);
	  ASSERT(frame->port == port);
	  READ_BRANCHES(branches);
	  MYNEWPRINTF("In INSW, port=%d, in_ebase=%p, branches=%lld\n", frame->port, in_ebase, branches);
	  in_ebase += sizeof(struct record_input);
	  cnt = cnt_backup;
	  size_t i;
      for(i=0;i<cnt*2;i++) *((uint8_t*)addr+i) = *(in_ebase+i);
	  in_ebase+=cnt*2;
	  in_entries_run++;
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER_DEBUG(level, glow, ghigh, eflags);
  }
  else
  {
	  asm volatile ("rep insw" : "+D" (addr), "+c" (cnt) : "d" (port) : "memory");
  }
}

/* Reads and returns 32 bits from PORT. */
static inline uint32_t
inl (uint16_t port)
{
  /* See [IA32-v2a] "IN". */
  uint32_t data;
  uint32_t glow, ghigh, level;
  unsigned long long branches;
  if(enable_record)
  {
	  bool context;
	  uint32_t flags, old_ecx;
	  MYNEWPRINTF_VARS;
	  INTR_GET_LEVEL(level);
	  INTR_DISABLE;
	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  asm volatile ("inl %w1, %0" : "=a" (data) : "Nd" (port));
	  struct record_input *frame = (struct record_input*)in_ebase;
	  frame->type = INL;
	  frame->port = port;
	  frame->size = 4;
	  frame->int_entry_no = log_entries;
	  in_ebase += sizeof(struct record_input);
	  *(uint32_t*)in_ebase = data;
	  in_ebase+=4;
	  in_entries++;
	  if(glow)
	  {
		  //Adjust for 1 extra branch on the replay path
		  INCREMENT_COUNTER(1);
	  }
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER(level, glow, ghigh);
  }
  else if(enable_replay)
  {
	  bool context;
	  uint32_t eflags, flags, old_ecx;
	  MYNEWPRINTF_VARS; 
	  INTR_GET_LEVEL(level);
	  INTR_DISABLE;

	  /* I don't want this piece of code to be single stepped */
	  GET_DEBUG_FLAG(eflags);
	  DISABLE_DEBUG;

	  DISABLE_COUNTERS1(glow,ghigh);
	  READ_ECX(old_ecx);
	  struct record_input *frame = (struct record_input*)in_ebase;
	  ASSERT(frame->type == INL);
	  ASSERT(frame->port == port);
	  MYNEWPRINTF("In INL, port=%d, in_ebase=%p\n", frame->port, in_ebase);
	  in_ebase += sizeof(struct record_input);
	  data = *((uint32_t*)in_ebase);
	  in_ebase+=4;
	  in_entries_run++;
	  WRITE_ECX(old_ecx);
	  INTR_SET_LEVEL_ENABLE_COUNTER_DEBUG(level, glow, ghigh, eflags);
  }
  else
  {
	  asm volatile ("inl %w1, %0" : "=a" (data) : "Nd" (port));
  }
  return data;
}

/* Reads CNT 32-bit (word) units from PORT, one after another,
   and stores them into the buffer starting at ADDR. */
static inline void
insl (uint16_t port, void *addr, size_t cnt)
{
  /* See [IA32-v2a] "INS". */
  asm volatile ("rep insl" : "+D" (addr), "+c" (cnt) : "d" (port) : "memory");
}

/* Writes byte DATA to PORT. */
static inline void
outb (uint16_t port, uint8_t data)
{
  /* See [IA32-v2b] "OUT". */
  asm volatile ("outb %b0, %w1 \n\t"
		  		: : "a" (data), "Nd" (port));
}

/* Writes to PORT each byte of data in the CNT-byte buffer
   starting at ADDR. */
static inline void
outsb (uint16_t port, const void *addr, size_t cnt)
{
  /* See [IA32-v2b] "OUTS". */
  asm volatile ("rep outsb" : "+S" (addr), "+c" (cnt) : "d" (port));
}

/* Writes the 16-bit DATA to PORT. */
static inline void
outw (uint16_t port, uint16_t data)
{
  /* See [IA32-v2b] "OUT". */
  asm volatile ("outw %w0, %w1" : : "a" (data), "Nd" (port));
}

/* Writes to PORT each 16-bit unit (halfword) of data in the
   CNT-halfword buffer starting at ADDR. */
static inline void
outsw (uint16_t port, const void *addr, size_t cnt)
{
  /* See [IA32-v2b] "OUTS". */
  asm volatile ("rep outsw" : "+S" (addr), "+c" (cnt) : "d" (port));
}

/* Writes the 32-bit DATA to PORT. */
static inline void
outl (uint16_t port, uint32_t data)
{
  /* See [IA32-v2b] "OUT". */
  asm volatile ("outl %0, %w1" : : "a" (data), "Nd" (port));
}

/* Writes to PORT each 32-bit unit (word) of data in the CNT-word
   buffer starting at ADDR. */
static inline void
outsl (uint16_t port, const void *addr, size_t cnt)
{
  /* See [IA32-v2b] "OUTS". */
  asm volatile ("rep outsl" : "+S" (addr), "+c" (cnt) : "d" (port));
}

#endif /* threads/io.h */
