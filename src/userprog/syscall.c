#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
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

static void syscall_handler (struct intr_frame *);

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

  /* Check first if pointer to syscall number is correct
     and pin it */
  void * addr = (void *) f->esp;
  validate_pointer(addr);
  protect_address(addr);

  /* Store stack pointer in case of a switch to kernel mode */
  struct thread * curr_thread = thread_current();
  curr_thread->curr_stack = f->esp;

  /* Check which syscall should be executed. Then, parse its arugments
     and execute the respective function with the arguments.
     Additionally, we check that the given addresses for the arguments
     are valid. The corresponding pages/frames for the addresses are
     loaded and pinned, so that they are (and will stay) available
     and no page fault should occur. */
 	switch(* (int*) addr) {
		case SYS_HALT:                   /* Halt the operating system. */
      sys_halt();
      break;
		case SYS_EXIT:                   /* Terminate this process. */
      parse_arguments(addr, argv, 1);
      sys_exit(argv[0]);
      break;
		case SYS_EXEC:                   /* Start another process. */
      parse_arguments(addr, argv, 1);
      validate_char_seq((void*) argv[0]);
      protect_char_sequence((char*) argv[0]);
      pid_t exec_ret = sys_exec((char*) argv[0]);
      unprotect_char_sequence((char*) argv[0]);
      f->eax = exec_ret;
      break;
		case SYS_WAIT:                  /* Wait for a child process to die. */
      parse_arguments(addr, argv, 1);
      int wait_ret = sys_wait(argv[0]);
      f->eax = wait_ret;
      break;
		case SYS_CREATE:                 /* Create a file. */
      parse_arguments(addr, argv, 2);
      validate_char_seq((void*) argv[0]);
      protect_char_sequence((char*) argv[0]);
      bool create_ret = sys_create((char*) argv[0], (unsigned) argv[1]);
      unprotect_char_sequence((char*) argv[0]);
			f->eax = create_ret;
      break;
		case SYS_REMOVE:                 /* Delete a file. */
      parse_arguments(addr, argv, 1);
      validate_char_seq((void*) argv[0]);
      protect_char_sequence((char*) argv[0]);
      bool remove_ret = sys_remove((char*) argv[0]);
      unprotect_char_sequence((char*) argv[0]);
			f->eax = remove_ret;
      break;
		case SYS_OPEN:                  /* Open a file. */
      parse_arguments(addr, argv, 1);
      validate_char_seq((void*) argv[0]);
      protect_char_sequence((char*) argv[0]);
      int open_ret = sys_open((char*) argv[0]);
      unprotect_char_sequence((char*) argv[0]);
			f->eax = open_ret;
      break;
		case SYS_FILESIZE:               /* Obtain a file's size. */
      parse_arguments(addr, argv, 1);
      int filesize_ret = sys_filesize(argv[0]);
			f->eax = filesize_ret;
      break;
		case SYS_READ:                   /* Read from a file. */
      parse_arguments(addr, argv, 3);
      validate_buffer((void*)argv[1], (unsigned) argv[2], true);
      protect_buffer((char*)argv[1], (unsigned) argv[2]);
      int read_ret = sys_read(argv[0], (char*) argv[1], (unsigned) argv[2]);
      unprotect_buffer((char*)argv[1], (unsigned) argv[2]);
			f->eax = read_ret;
      break;
		case SYS_WRITE:                  /* Write to a file. */
      parse_arguments(addr, argv, 3);
      validate_buffer((void*) argv[1], (unsigned) argv[2], false);
      protect_buffer((char*)argv[1], (unsigned) argv[2]);
      int write_ret = sys_write(argv[0], (char*) argv[1], (unsigned) argv[2]);
      unprotect_buffer((char*)argv[1], (unsigned) argv[2]);
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
      parse_arguments(addr, argv, 2);
      mmapid_t mmap_ret = sys_mmap(argv[0], (void*) argv[1]);
      f->eax = mmap_ret;
      break;
		case SYS_MUNMAP:                 /* Remove a memory mapping. */
      parse_arguments(addr, argv, 1);
      sys_munmap(argv[0]);
      break;
		case SYS_CHDIR:                  /* Change the current directory. */
      break;
		case SYS_MKDIR:                 /* Create a directory. */
      break;
		case SYS_READDIR:                /* Reads a directory entry. */
      break;
		case SYS_ISDIR:                 /* Tests if a fd represents a directory. */
      break;
		case SYS_INUMBER:                 /* Returns the inode number for a fd. */
      break;
		default:
			break;
	}

  unprotect_address(addr);

}

/* Gets the arguments from the stack and writes them into the argv array.
   As the pointer from the stack is dereferences, ints are written directly
   into argv while for char arguments the char pointer is written
   into argv. */
void parse_arguments(int* esp, int* argv, int argc) {
    int i;
    int* curr_ptr;
    for(i=0; i<argc; i++) {
      curr_ptr = esp + i + 1;
      validate_pointer((void *) curr_ptr);
      argv[i] = *curr_ptr;
    }
}

/* Iterates over a buffer and validates every address.
   Additionally, checks if every place of the buffer is
   writable if we want to write to the buffer. */
void validate_buffer(void* buff, unsigned length, bool writable)
{

  char * curr_ptr = (char *) buff;
  unsigned i;
  for(i=0; i<length; i++)
  {

    struct sup_page * sp = validate_pointer((void*) curr_ptr);
    curr_ptr++;

    if(sp == NULL)
    {
      sys_exit(-1);
      return;
    }

    if(!sp->writable && writable)
    {
      sys_exit(-1);
      return;
    }

  }

}

/* Checks for a char memory and a given length if the following addresses
   of the memory are also valid pointers. */
void validate_char_seq(void* seq)
{
  char* curr_ptr = (char*) seq;
  while(validate_and_get_char(curr_ptr) != 0)
    curr_ptr++;
}

/* Validates the given pointer and returns the char that the
   address points to. */
char validate_and_get_char(char* ptr)
{
  validate_pointer((void*) ptr);
  return *ptr;
}

/* Checks validity of pointer */
struct sup_page * validate_pointer(void* virt_addr)
{

  /* Checks that address is not null and within the user address space */
  if(virt_addr == NULL || !is_user_vaddr(virt_addr) || virt_addr < (void*) 0x08048000)
  {
    sys_exit(-1);
    return NULL;
  }

  /* Try to get the corresponding supplemental page for the given virtual address.
     If not it means that is has not been assigned before. Then, we have to check
     if it is a stack address and if so, extend it. */
  struct thread * curr_thread = thread_current();
  struct sup_page * sp = get_sup_page(&curr_thread->spt, virt_addr);

  /* Try to extend the stack. */
  if(sp == NULL)
  {
    struct thread * curr_thread = thread_current();
    void * curr_esp = curr_thread->curr_stack;
    bool extended = try_extend_stack(virt_addr, curr_esp, true);

    if(!extended)
    {
      sys_exit(-1);
      return NULL;
    }

    sp = get_sup_page(&curr_thread->spt, virt_addr);
  }

  return sp;

}

/* Depending on value, pins+loads or unpins the supplemental page
   that corresponds to the given address */
void set_address_pinning(void * virt_addr, bool value)
{
  if(value)
    load_page(&thread_current()->spt, virt_addr, true);
  else
    sup_page_pin_vaddr(virt_addr);
}

/* Iterates over a sequence of given length and pins+loads or unpins
   every address within buff and (buff+length). */
void set_sequence_pinning(char * buff, unsigned length, bool value)
{
  char * curr_ptr = buff;
  unsigned i;
  for(i=0; i<length; i++)
  {
    set_address_pinning((void*)curr_ptr, value);
    curr_ptr++;
  }
}

/* Pins+loads all supplemental pages for the buffer  */
void protect_buffer(char * buff, unsigned length)
{
  set_sequence_pinning(buff, length, true);
}

/* Unpins all supplemental pages for the buffer */
void unprotect_buffer(char * buff, unsigned length)
{
  set_sequence_pinning(buff, length, false);
}

/* Pins+loads all supplemental pages for the character sequence */
void protect_char_sequence(char* seq)
{
  unsigned length = strlen(seq);
  set_sequence_pinning(seq, length, true);
}

/* Unpins all supplemental pages for the character sequence */
void unprotect_char_sequence(char* seq)
{
  unsigned length = strlen(seq);
  set_sequence_pinning(seq, length, false);
}

/* Pins+loads the supplemental page for the given address */
void protect_address(void * virt_addr)
{
  set_address_pinning(virt_addr, true);
}

/* Unpins the supplemental page for the given address */
void unprotect_address(void * virt_addr)
{
  set_address_pinning(virt_addr, false);
}

/* Checks if the given fd is between the minimum fd
   value and the current fd counter of the thread.  */
bool check_bad_fd(int fd)
{
	if (fd >= 2 && fd < thread_current()->fd)
		return false;
	return true;
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
void close_all_thread_files(void)
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

/* Maps a file given by the file descriptor to the given virtual memory address */
mapid_t sys_mmap(int fd, void * addr)
{

  /* Check if file desriptor is alright, the address is within the user address space,
     the address is page aligned and not null */
  if(fd <= 1 || addr == NULL || !is_user_vaddr(addr) || addr < (void*) 0x08048000 || (int)addr % PGSIZE != 0)
    return -1;

  /* Maintain the mmap counter of the calling thread */
  struct thread * curr_thread = thread_current();
  mapid_t curr_mmapid = curr_thread->mmap_counter;
  curr_thread->mmap_counter++;

  /* Get file identified by the given file descriptor */
  acquire_harddrive_access();
  struct thread_file * t_file = get_file_from_process(fd);
  if(t_file == NULL || file_length(t_file->file) == 0)
  {
    release_harddrive_access();
    return -1;
  }

  /* Reoopen the file, so we use a clean file for the mapping.
     Otherwise, we could get a problem if the old file would be closed
     by the close operation on its file descriptor. */
  struct file * reopened_file = file_reopen(t_file->file);
  if(reopened_file == NULL || file_length(reopened_file) == 0)
  {
    release_harddrive_access();
    return -1;
  }

  /* Check is whole address space where we want to load the file in
     is availble in the supplemental page table */
  void * check_addr = addr;
  uint32_t check_bytes = file_length(reopened_file);
  while(check_bytes>0)
  {
    size_t page_check_bytes = check_bytes < PGSIZE ? check_bytes : PGSIZE;
    if(!sup_page_free(&curr_thread->spt, check_addr))
    {
      release_harddrive_access();
      return -1;
    }
    check_addr += PGSIZE;
    check_bytes -= page_check_bytes;
  }

  /* Actually read the file pagesize by pagesize and create a corresponding
     supplemental page. */
  off_t ofs = 0;
  uint32_t read_bytes = file_length(reopened_file);
  void * read_addr = addr;
  while(read_bytes > 0)
  {

    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    bool created_mmap = create_mmap_sup_page(&curr_thread->spt, read_addr, reopened_file, ofs, page_read_bytes, page_zero_bytes);

    if(!created_mmap)
    {
      release_harddrive_access();
      sys_munmap(curr_mmapid);
      return -1;
    }

    read_bytes -= page_read_bytes;
    read_addr += PGSIZE;
    ofs += page_read_bytes;

  }

  release_harddrive_access();

  /* Now we create a mmap struct that holds the information about the current mapping.
     It is needed when we want to unmap the current mapping since it holds information
     like for example the virtual address and the file length. From this we can compute
     all supplemental pages that we need to free. */
  struct thread_mmap_file * mf = malloc(sizeof(struct thread_mmap_file));
  if(mf == NULL)
    return -1;

  mf->mmap_id = curr_mmapid;
  mf->file = reopened_file;
  mf->virt_addr = addr;
  mf->file_length = file_length(t_file->file);
  list_push_back(&curr_thread->mmaps_list, &mf->elem);

  /* Return the mapping id of the recently created mapping. */
  return curr_mmapid;

}

void sys_munmap(mmapid_t map_id)
{

  /* Find corresponding mmap struct for current thread by iterating over
     all mmap structs of the thread. */
  struct thread * curr_thread = thread_current();
  struct thread_mmap_file * mf = NULL;
  struct list_elem *e;
  for (e = list_begin (&(curr_thread->mmaps_list)); e != list_end (&(curr_thread->mmaps_list)); e = list_next (e))
  {
			struct thread_mmap_file * curr_mf = list_entry (e, struct thread_mmap_file, elem);
			if (curr_mf->mmap_id == map_id)
      {
        mf = curr_mf;
        break;
      }
	}

  /* If no matching mapping struct could be found. */
  if(mf == NULL)
    return;

  /* Iterate over the whole file length and remove that corresponding supplemental pages
     that were created for the mapping. */
  uint32_t remove_bytes = mf->file_length;
  void * addr = mf->virt_addr;
  while(remove_bytes > 0)
  {
    size_t page_remove_bytes = remove_bytes < PGSIZE ? remove_bytes : PGSIZE;
    remove_mmap_sup_page(&curr_thread->spt, addr);
    remove_bytes -= page_remove_bytes;
    addr += PGSIZE;
  }

  /* Close the file that was reopened for the mapping.  */
  if(mf->file != NULL)
  {
    acquire_harddrive_access();
    file_close(mf->file);
    release_harddrive_access();
  }

  /* Remove/free the mapping struct for the mapping. */
  list_remove(&mf->elem);
  free(mf);

}
