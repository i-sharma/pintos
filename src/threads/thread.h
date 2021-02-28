#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING,  /* Running thread. */
  THREAD_READY,    /* Not running but ready to run. */
  /* begin ASSGN4 */
  THREAD_SLEEPING, /* Sleeping for specified time. */  
  /* end ASSGN4 */
  THREAD_BLOCKED,  /* Waiting for an event to trigger. */
  THREAD_DYING     /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* begin ASSGN4 */
/* Thread nice values. */
#define NICE_INIT 0  /* nice is 0 for initial thread */
#define NICE_MAX 20  /* Highest nice value is +20 (Min priority) */
#define NICE_MIN -20 /* Lowest nice value is -20 (Max priority) */
/* end ASSGN4 */

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
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
#endif

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */

  /* begin ASSGN4 */
  
  /* Alarm Clock Data Structures */
  int64_t wakeup_time;        /* Timer ticks till wakeup */
  struct list_elem sleepelem; /* List element for sleeping threads list. */

  /* MLFQ Scheduler Data Structures */
  /*
    Nice value for a thread. Ranges from NICE_MIN (-20) to NICE_MAX (+20)

    A higher nice value means the thread gives up its CPU time to other
    threads, a lower value means it takes away CPU time from others
   */
  int nice;

  /*
    Recent CPU - Store the amount of time a thread has used the CPU
    Maintain a moving average instead of simple values in order to give
    recent CPU usage a higher weight

    At every timer interrupt, we increment the recent_cpu for the running
    thread by 1

    Additionally, we recalculate the value of recent_cpu once every second
    for each thread with the below formula
    (Recalculate when timer_ticks % TIMER_FREQ = 0 since tests assume this)

    ```recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice```

    (Load avg is a global, which is also updated to reflect real CPU usage)

    We store 100 * recent_cpu to make it simpler to deal as an integer
  */
  int recent_cpu;

  /* List element for MLFQ Scheduler Lists */
  struct list_elem mlfqselem;

  /* The original priority of the thread. Need to handle donation */
  int orig_priority;

  /* The lock that given thread is waiting for, to donate/receive priority */
  struct lock *wait_lock;

  /* The thread that holds the lock `wait_lock` */
  struct thread *thread_lock;

  /* All possible donor threads */
  struct list donors_list;

  /* List element for donor lists */
  struct list_elem donorelem;
};

/*
  Load avg is a global, which is also updated to reflect real CPU usage

  We maintain a moving average and its value is updated once a second
  with the following formula. The value is initialized to 0 at boot
  (Recalculate when timer_ticks % TIMER_FREQ = 0 since tests assume this)

  ```load_avg = (59/60)*load_avg + (1/60)*ready_threads```

  We store 100 * recent_cpu to make it simpler to deal as an integer
*/
int load_avg;

/*
  Update the values of recent_cpu for all threads, when
  timer_ticks() % TIMER_FREQ == 0. We iterate over all the
  threads, update load_avg according to the given formula,
  and then update recent_cpu for each thread accordingly
*/
bool is_recent_cpu_update;

/*
  Update the MLFQS priorities for all threads, every 4th tick. We
  iterate over all threads and update their priorities using the formula
  mentioned in the documentation
*/
bool is_mlfqs_priority_update;
  /* end ASSGN4 */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void
thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

/* begin ASSGN4 */
void thread_sleep(void);
void thread_wake(struct thread *);
/* end ASSGN4 */

struct thread *
thread_current(void);
tid_t thread_tid(void);
const char *
thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void
thread_action_func(struct thread *t, void *aux);
void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

#endif /* threads/thread.h */
