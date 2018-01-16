#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <inttypes.h>
#include "threads/synch.h"
#include "vm/page.h"
#include "filesys/file.h"
#include "filesys/filesys.h"


/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Identifier for a memory mapping */
typedef int mmapid_t;

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    uint8_t *curr_stack;                /* Saved stack pointer during user mode */
    int priority;                       /* Priority. */
		int fd;															/* File index (greater as 1) */
    int64_t wake_up_tick;				/* Tick on which the thread should wake up again */
    struct list_elem allelem;           /* List element for all threads list. */
    struct list_elem waitelem;			/* List element for wait threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Defines the thread parent by whom the thread was created */
    tid_t parent_thread_tid;
    struct thread * parent_thread_ptr;

		/* The file containing the instructions of the thread (write-protected) */
		struct file* self_file;

    /* A list of all files that are opened by the thread */
	  struct list files_list;

    /* A list of all children threads of the current thread */
    struct list children_list;

    /* Pointer to child struct of current thread */
    struct child * own_child_struct;

    /* Used to synchronize the waiting of a parent for a child to exit */
	  struct semaphore sema_waiting;

    /* Used to synchronize the waiting of a parent for a child to start */
    struct semaphore sema_starting;

    /* Hash table that holds the supplemental pages for this thread */
    struct hash spt;

    /* A list of all mmap opened by the thread */
    struct list mmaps_list;

    /* Counter that is used to maintain the memory mapping ids */
    int mmap_counter;

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

  };

/* This is the struct that represents the child element for a thread.
   Every thread has one of this element. It contains further important information
   about the thread that are important for the parent thread.
   It also has the list element that is pushed into the
   parents chlidren list, so that a parent knows which children it has.*/
struct child
	{
    /* The thread id that identifies the thread */
		tid_t tid;
    /* Indicates if the thread already exited */
		bool is_exited;
    /* Holds the exit status of the thread, if it already exited */
		int exit_status;
    /* Indicates if the parent is waiting on this thread */
		bool someone_waiting;
    /* Holds the loading status of the thread,
       i.e. while it is not started its 0. A successful start
       sets it to 1, otherwise -1. */
		int loading_status;
    /* List element that is pushed into the children list of the parent thread */
	  struct list_elem elem;
	};

/* This struct is used by the OS to handle files. It contains a file
	 descriptor fd, which is automatically incremented by each thread
	 when it adds a new file to the thread. Moreover, a pointer to the
 	 actual file as well as the list element of files list is stored. */
struct thread_file
{
	/* File descriptor fd */
	int fd;
	/* Actual file */
  struct file *file;
	/* The list elem for files list */
  struct list_elem elem;
};

/* This struct is used to handle memory mappings. It represents a
   memory mapping and holds the specific information for the mapping
  (e.g. the mapped file and the virtual address where it is mapped on) */
struct thread_mmap_file
{
  /* Identifier for the current mapping */
  mmapid_t mmap_id;
  /* The file that belongs to this mapping */
  struct file * file;
  /* The virtual address where the file is mapped on */
  void * virt_addr;
  /* The length of the mapped file */
  int file_length;
  /* The list element for the mapping list of the thread */
  struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (int64_t ticks);
void thread_print_stats (void);

void thread_sleep(int64_t wake_up_tick);
void threads_try_wake(int64_t tick);
bool less_ticks (const struct list_elem *a,	const struct list_elem *b, void *aux UNUSED);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

bool thread_alive(tid_t tid);
struct thread * get_thread(tid_t tid);

void acquire_harddrive_access(void);
void release_harddrive_access(void);

#endif /* threads/thread.h */
