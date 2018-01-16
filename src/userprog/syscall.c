#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

/* Define prototypes */
static void syscall_handler (struct intr_frame *);
void sys_halt(void);
void sys_exit(int status);
pid_t sys_exec(char *cmd_line);
int sys_wait(pid_t pid);
bool sys_create(char *file, unsigned initial_size);
bool sys_remove(char *file);
int sys_open(char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);

void close_single_thread_file(int);
void close_all_thread_files(void);
int add_file_to_process(struct file*);
struct thread_file* get_file_from_process(int);
bool check_bad_fd(int);
void parse_arguments(int* esp, int* argv, int argc);
void validate_pointer(void* addr);
void validate_overflow(void* memory, unsigned length);
int get_addr_page(void* addr);


void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{

  /* Array where the parsed arguments are stored temporarily */
  int argv[3];

  /* Get address of syscall number */
  int * addr = (int *) f->esp;

  /* Check first if pointer to syscall number is correct */
  validate_pointer((void*) addr);

  /* Check which syscall should be executed. Then, parse its arugments
     and execute the respective function with the arguments */
 	switch(* addr) {
		case SYS_HALT:                   /* Halt the operating system. */
      sys_halt();
      break;
		case SYS_EXIT:                   /* Terminate this process. */
      parse_arguments(addr, argv, 1);
      sys_exit(argv[0]);
      break;
		case SYS_EXEC:                   /* Start another process. */
      parse_arguments(addr, argv, 1);
      pid_t exec_ret = sys_exec((char*) get_addr_page((void*) argv[0]));
      f->eax = exec_ret;
      break;
		case SYS_WAIT:                  /* Wait for a child process to die. */
      parse_arguments(addr, argv, 1);
      int wait_ret = sys_wait(argv[0]);
      f->eax = wait_ret;
      break;
		case SYS_CREATE:                 /* Create a file. */
      parse_arguments(addr, argv, 2);
      bool create_ret = sys_create((char*) get_addr_page((void*) argv[0]), (unsigned) argv[1]);
			f->eax = create_ret;
      break;
		case SYS_REMOVE:                 /* Delete a file. */
      parse_arguments(addr, argv, 1);
      bool remove_ret = sys_remove((char*) get_addr_page((void*) argv[0]));
			f->eax = remove_ret;
      break;
		case SYS_OPEN:                  /* Open a file. */
      parse_arguments(addr, argv, 1);
      int open_ret = sys_open((char*) get_addr_page((void*) argv[0]));
			f->eax = open_ret;
      break;
		case SYS_FILESIZE:               /* Obtain a file's size. */
      parse_arguments(addr, argv, 1);
      int filesize_ret = sys_filesize(argv[0]);
			f->eax = filesize_ret;
      break;
		case SYS_READ:                   /* Read from a file. */
      parse_arguments(addr, argv, 3);
      validate_overflow((void*)argv[1], (unsigned) argv[2]);
      int read_ret = sys_read(argv[0], (char*) get_addr_page((void*)argv[1]), (unsigned) argv[2]);
			f->eax = read_ret;
      break;
		case SYS_WRITE:                  /* Write to a file. */
      parse_arguments(addr, argv, 3);
      validate_overflow((void*)argv[1], (unsigned) argv[2]);
      int write_ret = sys_write(argv[0], (char*) get_addr_page((void*)argv[1]), (unsigned) argv[2]);
			f->eax = write_ret;
      break;
		case SYS_SEEK:                   /* Change position in a file. */
      parse_arguments(addr, argv, 2);
      sys_seek(argv[0], (unsigned) argv[1]);
      break;
		case SYS_TELL:                   /* Report current position in a file. */
      parse_arguments(addr, argv, 1);
      unsigned tell_ret = sys_tell(argv[0]);
			f->eax = tell_ret;
      break;
		case SYS_CLOSE:                 /* Close a file. */
      parse_arguments(addr, argv, 1);
      sys_close(argv[0]);
      break;
		case SYS_MMAP:                   /* Map a file into memory. */
		case SYS_MUNMAP:                 /* Remove a memory mapping. */
		case SYS_CHDIR:                  /* Change the current directory. */
		case SYS_MKDIR:                 /* Create a directory. */
		case SYS_READDIR:                /* Reads a directory entry. */
		case SYS_ISDIR:                 /* Tests if a fd represents a directory. */
		case SYS_INUMBER:                 /* Returns the inode number for a fd. */
		default:
      thread_exit ();
			break;
	}


}

/* Halts the systems */
void sys_halt() {
    shutdown_power_off();
}

/* Exits the current thread with the given status */
void sys_exit(int status) {

  /* Sets the status for the current thread only if the parent is alive.
     If its not alive it makes no sense, because no one will every access
     this state. */
  struct thread *curr_thread = thread_current ();
  if(thread_alive(curr_thread->parent_thread_tid))
  {
    curr_thread->own_child_struct->exit_status = status;
  }

  /* Finally, exiting thread. For more information see thread_exit() function. */
  printf ("%s: exit(%d)\n", curr_thread->name, status);
  thread_exit();

}

/* Executes a given command as child process */
pid_t sys_exec(char *cmd_line) {

  /* Execute the command. It also creates a child process executing the command. */
  pid_t pid = process_execute(cmd_line);

  /* Gets the recently created child fomr the children processes of the current thread */
  struct list_elem *e;
  struct thread * curr_thread = thread_current();
  struct child * curr_child = NULL;
  for (e = list_begin (&curr_thread->children_list); e != list_end (&curr_thread->children_list); e = list_next (e))
  {
    struct child *c = list_entry (e, struct child, elem);
    if (c->tid == pid)
      curr_child = c;
  }

  /* Recently created child should availble in children list
     (even if the starting failed). */
	ASSERT(curr_child != NULL);

  /* Waits for the created child to start/fail. If it sucessfully started
     returns the pid of the child (else return error status -1) */
 	if(curr_child->loading_status == 0)
    sema_down(&curr_thread->sema_starting);

	if(curr_child->loading_status == -1)
    return -1;

  return pid;

}

/* Waits for the given child to exit/terminate. For more details check the
   process_wait() function. */
int sys_wait(pid_t pid) {
  return process_wait(pid);
}

/* Creates a new file in the filesys given the file name and its initial size.
	 Returns true, if successful */
bool sys_create(char *file, unsigned initial_size) {
	acquire_harddrive_access();
	bool s = filesys_create(file, initial_size);
	release_harddrive_access();
  return s;
}

/* Removes a file with a given file name from the filesys.
	 Returns true, if successful */
bool sys_remove(char *file) {
	acquire_harddrive_access();
	bool s = filesys_remove(file);
	release_harddrive_access();
  return s;
}

/* Given a file name, open the file from the filesys, add to the files_list
	 of the current thread (with add_file_to_process(f)) and return the
 	 file descriptor value fd. */
int sys_open(char *file) {

	acquire_harddrive_access();

  struct file *f = filesys_open(file);

  if (f == 0)
    {
      release_harddrive_access();
      return -1;
    }

  int fd = add_file_to_process(f);
  release_harddrive_access();
  return fd;
}

/* Given a fd value, retrive the file and return it length. */
int sys_filesize(int fd) {

	acquire_harddrive_access();
	struct thread_file *tf = get_file_from_process(fd);

	if (tf->file == 0)
	{
		release_harddrive_access();
		return -1;
	}
	else
	{
		int s = file_length(tf->file);
		release_harddrive_access();
		return s;
	}
}

/* Reads size bytes from file with fd into buffer and
	 returns the actual size which was read. */
int sys_read(int fd, void *buffer, unsigned size) {

	/* Console read */
	if (fd == 0)
	{
		unsigned int i;
    uint8_t* buff = (uint8_t *) buffer;

		for (i = 0; i < size; i++)
		{
	  	buff[i] = input_getc();
		}

		return size;
	}

	/* Check if fd is valid */
	if (check_bad_fd(fd))
		sys_exit(-1);

	acquire_harddrive_access();

	struct thread_file *tf = get_file_from_process(fd);
	if (tf->file == 0)
	{
		release_harddrive_access();
		return -1;
	}

	int bytes_size = file_read(tf->file, buffer, size);
	release_harddrive_access();

  return bytes_size;
}

/* Writes size bytes into file with fd from buffer and
	 returns the actual size which was written. */
int sys_write(int fd, void *buffer, unsigned size) {

	if ((int*)buffer == NULL)
		sys_exit(-1);

	/* Console write */
	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}

	/* Check if fd is valid */
	if (check_bad_fd(fd))
		sys_exit(-1);

	acquire_harddrive_access();

	struct thread_file* tf = get_file_from_process(fd);

	if (tf->file == NULL)
	{
		release_harddrive_access();
		return -1;
	}

	int bytes_size = file_write(tf->file, buffer, size);
	release_harddrive_access();
	return bytes_size;
}

/* Given fd and the position, changes the next byte to be written/read in
	 the file to position */
void sys_seek(int fd, unsigned position) {
	acquire_harddrive_access();
  struct thread_file *tf = get_file_from_process(fd);
  if (tf->file == 0)
  {
    release_harddrive_access();
    return;
  }

  file_seek(tf->file, position);
  release_harddrive_access();
}

/* Given fd, return the position of the next byte to be written/read. */
unsigned sys_tell(int fd) {
	acquire_harddrive_access();
  struct thread_file *tf = get_file_from_process(fd);
  if (tf->file == 0)
  {
    release_harddrive_access();
    return 0;
  }

  unsigned offset = file_tell(tf->file);
  release_harddrive_access();
  return offset;
}

/* Closes the file corresponding to the given fd. */
void sys_close(int fd) {
  struct thread * curr_thread = thread_current();
	
	/* Checks if the file was really open by this thread, if not return without closing */
  bool owned = false;
  struct list_elem * e = list_begin(&curr_thread->files_list);
  for (e = list_begin (&curr_thread->files_list); e != list_end (&curr_thread->files_list); e = list_next (e))
  {
			struct thread_file *file = list_entry (e, struct thread_file, elem);
			if (file->fd == fd)
      {
        owned = true;
        break;
      }
	}

  if(!owned)
    return;

	acquire_harddrive_access();
	close_single_thread_file(fd);
	release_harddrive_access();
}

/* Closes a single thread file given fd. */
void close_single_thread_file(int fd)
{
	struct thread_file* tf = get_file_from_process(fd);
	file_close(tf->file);
	list_remove(&tf->elem);
	free(tf);
}

/* Closes all thread files of the current process. */
void close_all_thread_files()
{

	struct thread* t = thread_current();
	struct list_elem * e = list_begin(&t->files_list);
  struct list_elem * next_elem;

	while (e != list_end (&t->files_list))
  {
    next_elem = list_next(e);
		close_single_thread_file((list_entry (e, struct thread_file, elem))->fd);
		e = next_elem;
	}

}

/* Creates a new thread file given a file and adds it to current's thread files list. */
int add_file_to_process(struct file* f)
{
	struct thread_file *tf = malloc(sizeof(struct thread_file));
  tf->file = f;
  tf->fd = thread_current()->fd;
	
	/* Increment fd of current thread. */
  thread_current()->fd++;
  list_push_back(&thread_current()->files_list, &tf->elem);
  return tf->fd;
}

/* Gets thread file from current process given fd. */
struct thread_file* get_file_from_process(int fd)
{
	struct thread* t = thread_current();

	struct list_elem *e;
  for (e = list_begin (&(t->files_list)); e != list_end (&(t->files_list)); e = list_next (e))
  {
			struct thread_file *f = list_entry (e, struct thread_file, elem);

			if (f->fd == fd)
				return f;
	}

	return NULL;
}

/* Checks if the given fd is between the minimum fd value and the current fd counter of the thread.  */
bool check_bad_fd(int fd)
{
	if (fd >= 2 && fd < thread_current()->fd)
		return false;
	return true;
}

/* Gets the arguments from the stack and writes them into the argv array.
   As the pointer from the stack is dereferences, ints are written directly
   into argv while for char arguments the char pointer is written
   into argv */
void parse_arguments(int* esp, int* argv, int argc) {
    int i;
    int* curr_ptr;
    for(i=0; i<argc; i++) {
      curr_ptr = esp + i + 1;
      validate_pointer((void *) curr_ptr);
      argv[i] = *curr_ptr;
    }
}

/* In fact, checks the address by trying to get the corresponding kernel
   virtual address. For more details about the check see get_addr_page() function. */
void validate_pointer(void* addr)
{
  get_addr_page(addr);
}

/* Checks for a char memory and a given length if the following addresses
   of the memory are also valid pointers. More precisely, it checks for every
   pointer within (memory) and (memory+length-1) to be valid. */
void validate_overflow(void* memory, unsigned length)
{
  char * curr_ptr = (char *) memory;
  unsigned i;
  for(i=0; i<length; i++)
  {
    validate_pointer((void*) curr_ptr);
    curr_ptr++;
  }
}

/* Checks validity of pointer and gets the kernel virtual address corresponding
   to the given address. */
int get_addr_page(void* addr)
{

  /* Checks if the given address is not NULL, within the virtual user address
     space and above the start of the virtual user address space.
     If not exit thread with an error. */
  if(addr == NULL || !is_user_vaddr(addr) || addr < (void*) 0x08048000)
  {
    sys_exit(-1);
    return 0;
  }

  /* Tries to get the corresponding kernel virtual address for the
     given address. If sucessfully, returns the pointer to the
     kernel virtual address. If fails exit thread with error status.
     If sucessfully return pointer to kernel virtual address. */
  struct thread * curr_thread = thread_current();
  void* ptr = pagedir_get_page(curr_thread->pagedir, addr);
  if(ptr == NULL)
  {
    sys_exit(-1);
    return 0;
  }

  return (int)ptr;
}
