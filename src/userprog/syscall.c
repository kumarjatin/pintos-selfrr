#include "userprog/syscall.h"
//101516485726688
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
//shashank
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "userprog/process.h"

#define PID_ERROR ((pid_t) -1)

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// shivanker
// Terminates Pintos by calling power_off()
static void 
halt_syscall(void)	{
	power_off();
}

//shashank
//Terminates the current user program, returning status to the kernel. If the process's parent 
//waits for it (see below), this is the status that will be returned. Conventionally, a status 
//of 0 indicates success and nonzero values indicate errors.
void 
user_thread_exit(int status)	{
	//pass status to parent's wait
        struct thread *cur = thread_current ();
        struct thread *parent = get_thread_locked(cur->parent); //returns pointer to parent precess
        if(parent != NULL){ //if parent = NULL then the parent is no longer alive
                struct child *self = get_child(parent, (pid_t)cur->tid);
                if(self != NULL){
                        self->exit_code = status; //set the exitcode as the status
                }else{
                        printf("Child thread is not present in the parent's children list\n");
                }

		lock_release(&parent->child_lock);
        }
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();	
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

// shivanker
/* checks that the pointer uaddr is below PHYS_BASE, not NULL,
   and is present. If not, print error and kill */
static void
check_user_read(const void *uaddr)	{
  if(!is_user_vaddr(uaddr))
    user_thread_exit (-1);
  if(get_user((uint8_t*)uaddr) == -1)
    user_thread_exit (-1);
}

static void
check_user_write(const void* uaddr)	{
  if(!is_user_vaddr(uaddr))
    user_thread_exit (-1);
  if(put_user((uint8_t*)uaddr, (uint8_t)0) == false)
    user_thread_exit (-1);
}

static void
check_user_string(const char *s)	{

	check_user_read((void*)s);
	while(*s != '\0')	{
		s++;
		// now we only need to check validity of pointer when it reaches a page boundary
		if((uint32_t)s % (uint32_t)PGSIZE == 0)
			check_user_read((void*)s);
	}

	return;
}

static void
check_user_buffer(const void* buffer, unsigned size, bool write)	{
	if(size == 0)
		return;

	void (*check_func)(const void*) = write ? check_user_write : check_user_read;
	const void *end = buffer + size;
	check_func(buffer);
	while(buffer + PGSIZE < end)
		check_func(buffer = buffer + PGSIZE);
	check_func(end - 1);

	return;
}

//shashank
/* Runs the executable whose name is given in cmd_line, passing any given 
arguments, and returns the new process's program id (pid). Must return 
pid -1, which otherwise should not be a valid pid, if the program cannot 
load or run for any reason. Thus, the parent process cannot return from 
the exec until it knows whether the child process successfully loaded its 
executable. You must use appropriate synchronization to ensure this. */
static pid_t
exec_syscall(const char *cmd_line)      {
        check_user_string(cmd_line);
        pid_t process_id = ((pid_t) process_execute(cmd_line));
        return process_id;
}

//shashank
//If process pid is still alive, waits until it dies. Then, returns the status 
//that pid passed to exit, or -1 if pid was terminated by the kernel (e.g. killed 
//due to an exception). If pid does not refer to a child of the calling thread, 
//or if wait has already been successfully called for the given pid, returns -1 
//immediately, without waiting.
static int
wait_syscall(pid_t pid)	{
	return process_wait((tid_t) pid);
}

//shashank
/* Creates a new file called file initially initial_size bytes in size. 
Returns true if successful, false otherwise. Creating a new file does 
not open it: opening the new file is a separate operation which would 
require a open system call. */
static bool
create_syscall (const char *file, unsigned initial_size)	{
	// check for validity of file+strlen(file) but without using strlen
	check_user_string(file);

	lock_acquire(&filesys_lock);
        bool retval = filesys_create(file, initial_size);
	lock_release(&filesys_lock);

	return retval;
}

//shashank
//Deletes the file called file. Returns true if successful, false otherwise.
// A file may be removed regardless of whether it is open or closed, and 
//removing an open file does not close it.
static bool
remove_syscall (const char *file)	{
	// check for validity of file+strlen(file) but without using strlen
	check_user_string(file);

	lock_acquire(&filesys_lock);
        bool retval = filesys_remove(file);
	lock_release(&filesys_lock);

	return retval;
}

//shashank
//Opens the file called file. Returns a nonnegative integer handle called a 
//"file descriptor" (fd), or -1 if the file could not be opened.
static int
open_syscall (const char *file) {
	// check for validity of file+strlen(file) but without using strlen
	check_user_string(file);
        
	//now search for the lowest possible fd
        struct thread *cur = thread_current ();
        int i=2; //Leaving 0 and 1 for STDIN_FILENO and STDOUT_FILENO
        for(i=2; i<128; i++){
                if(cur->fd_table[i] == NULL){
                        break; //found the required fd
                }
        }
        if(i==128){
                //not able to find any free entry in fd table
                return -1;
        }

	lock_acquire(&filesys_lock);
        struct file *newFile = filesys_open(file);
	lock_release(&filesys_lock);

        if(newFile == NULL){
                //failed to open the file with given file name
		return -1;
        }
        
	//map lowest free fd to given file
        cur->fd_table[i] = newFile;
        return i; //return fd
}

//shashank
//Returns the size, in bytes, of the file open as fd.
static int
filesize_syscall (int fd){
	struct thread *cur = thread_current ();
	// verify fd to be in range
	if(fd >= 128 || fd < 0)
		user_thread_exit(-1);
        struct file *filePointer = cur->fd_table[fd];

	if(filePointer == NULL)	{
		// bad fd - shivanker
		user_thread_exit(-1);
	}

	lock_acquire(&filesys_lock);
        int size = (int) file_length(filePointer); //function in file.h
	lock_release(&filesys_lock);

        return size;
}

//shashank
//Reads size bytes from the file open as fd into buffer. Returns the number of bytes actually 
//read (0 at end of file), or -1 if the file could not be read (due to a condition other than 
//end of file). Fd 0 reads from the keyboard using input_getc().
static int
read_syscall (int fd, void *buffer, unsigned size) {
	// check this first, otherwise the check_user_write calls don't make sense
	if(size == 0)
		return 0;

	check_user_buffer(buffer, size, true);

        if(fd == 0){
                //read from keyboard
                unsigned i;
                for(i=0; i<size; i++){
                        ((char*)buffer)[i] = (char) input_getc();
                }
                return size;
                //Note that the red bytes will always be equal to size
        }else{
                struct thread *cur = thread_current ();
		// verify fd to be in range
		if(fd >= 128 || fd < 0)
			user_thread_exit(-1);
                struct file *filePointer = cur->fd_table[fd];

		if(filePointer == NULL)	{
			// bad fd - shivanker
			user_thread_exit(-1);
		}

		lock_acquire(&filesys_lock);
                int readBytes = (int) file_read (filePointer, buffer, (off_t) size);
		lock_release(&filesys_lock);

                return readBytes;
        }
        //return 0;
}

//shivanker
//shashank
//Writes size bytes from buffer to the open file fd. Returns the number of bytes actually 
//written, which may be less than size if some bytes could not be written.
static int
write_syscall (int fd, const void *buffer, unsigned size)	{
	// check this first, otherwise the check_user_write calls don't make sense
	if(size == 0)
		return 0;

	check_user_buffer(buffer, size, false);

	int written = 0;
	if (fd == 1)	{
                //write to console
		char* mybuf = (char*)buffer;
		while(size > 256){
			putbuf(mybuf, 256);
			size -= 256;
			mybuf += 256;
			written += 256;
		}
		putbuf(mybuf, size);
		written += size;
	}
	else	{
		struct thread *cur = thread_current ();
		// verify fd to be in range
		if(fd >= 128 || fd < 0)
			user_thread_exit(-1);
                struct file *filePointer = cur->fd_table[fd];
                if(filePointer == NULL){
			// bad fd - shivanker
			user_thread_exit(-1);
                }

		lock_acquire(&filesys_lock);
                written = (int) file_write (filePointer, buffer, (off_t) size);
		lock_release(&filesys_lock);
	}
	return written;
}

//shashank
//Changes the next byte to be read or written in open file fd to position, expressed 
//in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
static void
seek_syscall (int fd, unsigned position) {
	struct thread *cur = thread_current ();
	// verify fd to be in range
	if(fd >= 128 || fd < 0)
		user_thread_exit(-1);
        struct file *filePointer = cur->fd_table[fd];
        off_t pos = (off_t) position;
	if(filePointer == NULL){
		// bad fd - shivanker
		user_thread_exit(-1);
	}

	lock_acquire(&filesys_lock);
        file_seek(filePointer, pos);
	lock_release(&filesys_lock);
}

//shashank
//Returns the position of the next byte to be read or written in open file fd, 
//expressed in bytes from the beginning of the file.
static unsigned
tell_syscall (int fd)	{
	struct thread *cur = thread_current ();
	// verify fd to be in range
	if(fd >= 128 || fd < 0)
		user_thread_exit(-1);
        struct file *filePointer = cur->fd_table[fd];
	if(filePointer == NULL){
		// bad fd - shivanker
		user_thread_exit(-1);
	}

	lock_acquire(&filesys_lock);
        unsigned offset = (unsigned) file_tell(filePointer); //function in file.h
	lock_release(&filesys_lock);

        return offset;
}

//shashank
//Closes file descriptor fd. Exiting or terminating a process implicitly 
//closes all its open file descriptors, as if by calling this function for each one.
static void
close_syscall (int fd)	{
	struct thread *cur = thread_current ();
	// verify fd to be in range
	if(fd >= 128 || fd < 0)
		user_thread_exit(-1);
        struct file *filePointer = cur->fd_table[fd];
	if(filePointer == NULL){
		// bad fd - shivanker
		user_thread_exit(-1);
	}
	// shivanker
	lock_acquire(&filesys_lock);
	file_close(filePointer);
	lock_release(&filesys_lock);

        cur->fd_table[fd] = NULL;
        return;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  check_user_read(f->esp); // shivanker
  check_user_read(f->esp + 3);
  //If I am reaching here, then the user pointer is correct.
  //shashank  
  int syscall_number = *((uint32_t*)(f->esp));
  switch (syscall_number){
    case SYS_HALT:	 /* Halt the operating system. */
                      halt_syscall();
                      break;
    case SYS_EXIT:	 /* Terminate this process. */
                      check_user_read(f->esp + 7);
                      //int arg0 = *((int*)(f->esp + 4));
                      user_thread_exit(*((int*)(f->esp + 4)));
                      break;
    case SYS_EXEC:	 /* Start another process. */
                      check_user_read(f->esp + 7);
                      //char* arg0 = *((char**)(f->esp + 4));
                      f->eax = (uint32_t) exec_syscall(*((char**)(f->esp + 4)));
                      break;
    case SYS_WAIT:	 /* Wait for a child process to die. */
                      check_user_read(f->esp + 7);
                      //pid_t arg0 = *((pid_t*)(f->esp + 4));
                      f->eax = (uint32_t) wait_syscall(*((pid_t*)(f->esp + 4)));
                      break;
    case SYS_CREATE:	/* Create a file. */
                      check_user_read(f->esp + 11);
                      //char* arg0 = *((char**)(f->esp + 4));
                      //unsigned arg1 = *((unsigned*)(f->esp + 8));
                      f->eax = (uint32_t) create_syscall(*((char**)(f->esp + 4)), *((unsigned*)(f->esp + 8)));
                      break;
    case SYS_REMOVE:	/* Delete a file. */
                      check_user_read(f->esp + 7);
                      //char* arg0 = *((char**)(f->esp + 4));
                      f->eax = (uint32_t) remove_syscall(*((char**)(f->esp + 4)));
                      break;
    case SYS_OPEN:	  /* Open a file. */
                      check_user_read(f->esp + 7);
                      //char* arg0 = *((char**)(f->esp + 4));
                      f->eax = (uint32_t) open_syscall(*((char**)(f->esp + 4)));
                      break;
    case SYS_FILESIZE:/* Obtain a file's size. */
                      check_user_read(f->esp + 7);
                      //int arg0 = *((int*)(f->esp + 4));
                      f->eax = (uint32_t) filesize_syscall(*((int*)(f->esp + 4)));
                      break;
    case SYS_READ:	  /* Read from a file. */
                      check_user_read(f->esp + 15);
                      //int arg0 = *((int*)(f->esp + 4));
                      //void* arg1 = *((void**)(f->esp + 8));
                      //unsigned arg2 = *((unsigned*)(f->esp + 12));
                      f->eax = (uint32_t) read_syscall(*((int*)(f->esp + 4)), *((void**)(f->esp + 8)), *((unsigned*)(f->esp + 12)));
                      break;
    case SYS_WRITE:     /* Write to a file. */
                        check_user_read(f->esp + 15);
                        //int arg0 = *((int*)(f->esp + 4));
                        //void* arg1 = *((void**)(f->esp + 8));
                        //unsigned arg2 = *((unsigned*)(f->esp + 12));
                        f->eax = (uint32_t) write_syscall(*((int*)(f->esp + 4)), *((void**)(f->esp + 8)), *((unsigned*)(f->esp + 12)));
                        break;
    case SYS_SEEK:	/* Change position in a file. */
              		check_user_read(f->esp + 11);
              		//int arg0 = *((int*)(f->esp + 4));
              		//unsigned arg1 = *((unsigned*)(f->esp + 8);
              		seek_syscall(*((int*)(f->esp + 4)), *((unsigned*)(f->esp + 8)));
              		break;
    case SYS_TELL:	/* Report current position in a file. */
              		check_user_read(f->esp + 7);
              		//int arg0 = *((int*)(f->esp + 4));
              		f->eax = (uint32_t) tell_syscall(*((int*)(f->esp + 4)));
              		break;
    case SYS_CLOSE:	/* Close a file. */
              		check_user_read(f->esp + 7);
              		//int arg0 = *((int*)(f->esp + 4));
              		close_syscall(*((int*)(f->esp + 4)));
              		break;
    default:		/* Default case*/
                        //printf("Current Syscall has no handler specified: %d\n", syscall_number);
                        user_thread_exit (-1);
  }
  return;

}
