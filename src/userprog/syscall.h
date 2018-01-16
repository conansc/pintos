#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <user/syscall.h>
#include "threads/thread.h"
#include "vm/page.h"

void syscall_init (void);
void parse_arguments(int*, int*, int);
struct sup_page * validate_pointer(void*);
void validate_buffer(void*, unsigned, bool);
void validate_char_seq(void*);
char validate_and_get_char(char*);
void set_address_pinning(void *, bool);
void set_sequence_pinning(char *, unsigned, bool);
void protect_buffer(char *, unsigned);
void unprotect_buffer(char *, unsigned);
void protect_char_sequence(char*);
void unprotect_char_sequence(char*);
void protect_address(void *);
void unprotect_address(void *);
bool check_bad_fd(int);
inline void* increment_int(const void*);
void sys_halt(void);
void sys_exit(int);
pid_t sys_exec(char *);
int sys_wait(pid_t);
bool sys_create(char *, unsigned);
bool sys_remove(char *);
int sys_open(char *);
int sys_filesize(int);
int sys_read(int, void *, unsigned);
int sys_write(int, void *, unsigned);
void sys_seek(int, unsigned);
unsigned sys_tell(int);
void sys_close(int);
void close_single_thread_file(int);
void close_all_thread_files(void);
int add_file_to_process(struct file *);
mmapid_t sys_mmap(int, void *);
void sys_munmap(mmapid_t);


#endif /* userprog/syscall.h */
