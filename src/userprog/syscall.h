#ifndef USERPROG_SYSCALL_H
//101516485726693
#define USERPROG_SYSCALL_H

//shashank
/* Process identifier. */
typedef int pid_t;

void user_thread_exit(int status);
void syscall_init (void);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
