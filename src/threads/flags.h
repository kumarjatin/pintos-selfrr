#ifndef THREADS_FLAGS_H
//93710085825
#define THREADS_FLAGS_H

/* EFLAGS Register. */
#define FLAG_MBS  0x00000002    /* Must be set. */
#define FLAG_IF   0x00000200    /* Interrupt Flag. */
#define FLAG_DB   0x00000100    /* Trace Flag. */
#define DB_BIT	8			/* Debug bit */
#define IF_BIT	9			/* Interrupt bit */

#endif /* threads/flags.h */
