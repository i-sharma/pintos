#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
/* begin ASSGN4 */
#include "devices/timer.h"
#include "lib/fp_arithmetic.h"
/* end ASSGN4 */
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
  void *eip;             /* Return address. */
  thread_func *function; /* Function to call. */
  void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void
kernel_thread(thread_func *, void *aux);

static void
idle(void *aux UNUSED);
static struct thread *
running_thread(void);
static struct thread *
next_thread_to_run(void);
static void
init_thread(struct thread *, const char *name, int priority);
static bool
is_thread(struct thread *) UNUSED;
static void *
alloc_frame(struct thread *, size_t size);
static void
schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t
allocate_tid(void);

/* begin ASSGN4 */
/* Custom Prototypes and Variables */

/* Clamp x such that min <= x <= max */
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

/* Multiply by this factor when returning load_avg and recent_cpu */
int MULTIPLICATION_FACTOR;

/* MLFQ Scheduler Queues */
static struct list mlfqs_lists[PRI_MAX + 1];

/* Create a new thread whose only job is to schedule */
struct thread *mlfqs_thread;

/* The mlfqs_scheduler that is assigned to the mlfqs_thread */
void mlfqs_scheduler(void *arg UNUSED);

/* Update the priorities of a single thread, using the MLFQS equations */
void thread_update_priority(struct thread *t);

/* Update the priorities of each thread, using the MLFQS equations */
void thread_update_priorities(void);

/* Update the recent_cpu of each thread, using the
  formulas mentioned in the documentation */
void thread_update_recent_cpu(void);

/* Update global load_avg using the formula mentioned in the documentation */
void thread_update_load_avg(void);

/* Get the highest priority level present in the MLFQ Scheduling lists */
int thread_mlfqs_get_highest_priority(void);

/* For priority scheduling - Compare priorities of 2 threads */
static bool thread_compare_priorities(struct list_elem *a, struct list_elem *b, void *aux UNUSED);
/* end ASSGN4 */

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
  ASSERT(intr_get_level() == INTR_OFF);

  lock_init(&tid_lock);
  list_init(&ready_list);
  list_init(&all_list);
  /* begin ASSGN4 */
  if (thread_mlfqs)
  {
    int i;
    for (i = PRI_MIN; i <= PRI_MAX; i++)
      list_init(&mlfqs_lists[i]);
  }

  /* Set the value of load_avg to be 0 at boot */
  load_avg = 0;

  /* Initialize updates to false */
  is_recent_cpu_update = false;
  is_mlfqs_priority_update = false;
  MULTIPLICATION_FACTOR = 100;
  /* end ASSGN4 */

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);
  /* begin ASSGN4 */
  /*
    Create a new thread, whose only job is to wake up every 4 ticks,
    recalculate MLFQ priorities and fix them. Assign this thread to the
    `mlfqs_scheduler` function and give it max priority (PRI_MAX)
   */
  thread_create("mlfqs_thread", PRI_MAX, mlfqs_scheduler, NULL);
  /* end ASSGN4 */
  
  /* Start preemptive thread scheduling. */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
  struct thread *t = thread_current();
  /* begin ASSGN4 */
  t->recent_cpu = INT_ADD(t->recent_cpu, 1);  
  /* end ASSGN4 */

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;
  /* begin ASSGN4 */
  int64_t ticks = timer_ticks();

  if (ticks % TIMER_FREQ == 0)
    is_recent_cpu_update = true;
  /* end ASSGN4 */
  
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
  {
    /* begin ASSGN4 */
    is_mlfqs_priority_update = true;    
    /* end ASSGN4 */
    intr_yield_on_return();
  }
  /* begin ASSGN4 */
  if (thread_mlfqs && (is_mlfqs_priority_update || is_recent_cpu_update))
  {
    // printf("Update CPU: %d | Priority Update: %d | ticks: %ld\n", is_recent_cpu_update, is_mlfqs_priority_update, ticks);
    thread_unblock(mlfqs_thread);
    intr_yield_on_return();
  }
  /* end ASSGN4 */
  
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
         idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT(function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread(t, name, priority);
  tid = t->tid = allocate_tid();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack'
     member cannot be observed. */
  old_level = intr_disable();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level(old_level);

  /* Add to run queue. */
  thread_unblock(t);
  /* begin ASSGN4 */
  old_level = intr_disable();

  if (t->priority > thread_current()->priority && (t != idle_thread))
    thread_yield();

  intr_set_level(old_level);

  return tid;
  /* end ASSGN4 */
  
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;
  schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();

  ASSERT(t->status == THREAD_BLOCKED);

  /* begin ASSGN4 */
  if (!thread_mlfqs)
    list_insert_ordered(&ready_list, &t->elem, thread_compare_priorities, NULL);
  else
    list_push_back(&mlfqs_lists[t->priority], &t->mlfqselem);

  t->status = THREAD_READY;
  intr_set_level(old_level);
  /* end ASSGN4 */
  
}

/* begin ASSGN4 */
/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by `thread_wake()`

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_sleep(void)
{
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_SLEEPING;
  schedule();
}

/* Transitions a sleeping thread T to the ready-to-run state.
   This is an error if T is not sleeping */
void thread_wake(struct thread *t)
{
  ASSERT(is_thread(t));

  enum intr_level old_level = intr_disable();

  ASSERT(t->status == THREAD_SLEEPING);

  if (!thread_mlfqs)
    list_push_back(&ready_list, &t->elem);
  else
    list_push_back(&mlfqs_lists[t->priority], &t->mlfqselem);

  t->status = THREAD_READY;

  intr_set_level(old_level);
}
/* end ASSGN4 */


/* Returns the name of the running thread. */
const char *
thread_name(void)
{
  return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
  struct thread *t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
  return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
  ASSERT(!intr_context());

#ifdef USERPROG
  process_exit();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable();
  list_remove(&thread_current()->allelem);
  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
  struct thread *cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread)
  {
    /* begin ASSGN4 */
    if (!thread_mlfqs)
      list_insert_ordered(&ready_list, &cur->elem, thread_compare_priorities, NULL);
    else
      list_push_back(&mlfqs_lists[cur->priority], &cur->mlfqselem);
    /* end ASSGN4 */
  }
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
  {
    struct thread *t = list_entry(e, struct thread, allelem);
    func(t, aux);
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
  /* begin ASSGN4 */
  struct thread *curr = thread_current();

  enum intr_level old_level = intr_disable();

  curr->orig_priority = CLAMP(new_priority, PRI_MIN, PRI_MAX);

  if (list_empty(&curr->donors_list) || new_priority > curr->priority)
  {
    curr->priority = new_priority;
  }

  if (!list_empty(&ready_list))
  {
    struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);
    if (front->priority > thread_current()->priority)
      thread_yield();
  }

  intr_set_level(old_level);  
  /* end ASSGN4 */
  
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
  /* begin ASSGN4 */
  struct thread *curr = thread_current();
  return curr->priority;
  /* end ASSGN4 */
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
  /* begin ASSGN4 */
  struct thread *curr = thread_current();

  enum intr_level old_level = intr_disable();

  curr->nice = CLAMP(nice, NICE_MIN, NICE_MAX);

  thread_update_priority(curr);

  int highest_priority = thread_mlfqs_get_highest_priority();
  if (highest_priority != -1 && curr->priority < highest_priority)
    thread_yield();

  intr_set_level(old_level);
  /* end ASSGN4 */
  
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
  /* begin ASSGN4 */
  struct thread *curr = thread_current();
  return curr->nice;
  /* end ASSGN4 */
}
/* begin ASSGN4 */
/* Returns MULTIPLICATION_FACTOR times the system load average. */
int thread_get_load_avg(void)
{
  return ROUND_CLOSEST(INT_MULTIPLY(load_avg, MULTIPLICATION_FACTOR));
}

/* Returns MULTIPLICATION_FACTOR times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
  struct thread *curr = thread_current();
  return ROUND_CLOSEST(INT_MULTIPLY(curr->recent_cpu, MULTIPLICATION_FACTOR));
}
/* end ASSGN4 */

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;)
  {
    /* Let someone else run. */
    intr_disable();
    thread_block();

    /* Re-enable interrupts and wait for the next one.

   The `sti' instruction disables interrupts until the
   completion of the next instruction, so these two
   instructions are executed atomically.  This atomicity is
   important; otherwise, an interrupt could be handled
   between re-enabling interrupts and waiting for the next
   one to occur, wasting as much as one clock tick worth of
   time.

   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
   7.11.1 "HLT Instruction". */
    asm volatile("sti; hlt"
                 :
                 :
                 : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread(void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0"
      : "=g"(esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread(struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t *)t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* begin ASSGN4 */
  t->orig_priority = priority;
  list_init(&t->donors_list);
  t->wait_lock = NULL;
  t->thread_lock = NULL;

  /* Custom defined values */
  if (t->tid == initial_thread->tid)
  {
    t->nice = NICE_INIT; /* The initial thread has a nice value of 0 */
    t->recent_cpu = 0;   /* The initial thread has a recent CPU value of 0 */
  }
  else
  {
    /* Inherit nice and recent_cpu from parent */
    t->nice = thread_current()->nice;
    t->recent_cpu = thread_current()->recent_cpu;
  }

  ASSERT(is_thread(t)); /* TODO: Remove this */
  /* end ASSGN4 */

  list_push_back(&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame(struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT(is_thread(t));
  ASSERT(size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
  /* begin ASSGN4 */
  if (!thread_mlfqs)
  {
    if (list_empty(&ready_list))
      return idle_thread;
    else
      return list_entry(list_pop_front(&ready_list), struct thread, elem);
  }
  else
  {
    int highest_priority = thread_mlfqs_get_highest_priority();

    if (highest_priority != -1)
      return list_entry(list_pop_front(&mlfqs_lists[highest_priority]), struct thread, mlfqselem);
    else
      return idle_thread;
  }
  /* end ASSGN4 */
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_schedule_tail(struct thread *prev)
{
  struct thread *cur = running_thread();

  ASSERT(intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
  {
    ASSERT(prev != cur);
    palloc_free_page(prev);
  }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule(void)
{
  struct thread *cur = running_thread();
  struct thread *next = next_thread_to_run();
  struct thread *prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));

  if (cur != next)
    prev = switch_threads(cur, next);
  thread_schedule_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

/* begin ASSGN4 */

void mlfqs_scheduler(void *arg UNUSED)
{
  mlfqs_thread = thread_current();

  for (;;)
  {
    enum intr_level old_level = intr_disable();
    thread_block();
    intr_set_level(old_level);

    if (is_recent_cpu_update)
    {
      thread_update_load_avg();
      thread_update_recent_cpu();
    }

    if (is_mlfqs_priority_update)
      thread_update_priorities();
  }
}

void thread_update_load_avg(void)
{
  int ready_threads = 0;

  struct list_elem *iter;
  for (iter = list_begin(&all_list); iter != list_end(&all_list); iter = list_next(iter))
  {
    struct thread *curr = list_entry(iter, struct thread, allelem);
    tid_t curr_tid = curr->tid;

    if (curr->status == THREAD_READY && curr_tid != wakeup_thread->tid && curr_tid != mlfqs_thread->tid && curr_tid != idle_thread->tid)
      ready_threads++;
  }

  struct thread *curr = thread_current();
  tid_t curr_tid = curr->tid;

  if (curr_tid != wakeup_thread->tid && curr_tid != mlfqs_thread->tid && curr_tid != idle_thread->tid)
    ready_threads++;

  int64_t n = INT_ADD(INT_MULTIPLY(load_avg, 59), ready_threads);
  load_avg = INT_DIVIDE(n, 60);

  // printf("Load average is %d and number of threads is %d\n", load_avg, ready_threads);
}

void thread_update_recent_cpu(void)
{
  is_recent_cpu_update = false;

  struct list_elem *iter;
  for (iter = list_begin(&all_list); iter != list_end(&all_list); iter = list_next(iter))
  {
    struct thread *curr = list_entry(iter, struct thread, allelem);
    tid_t curr_tid = curr->tid;

    if (curr_tid != wakeup_thread->tid && curr_tid != mlfqs_thread->tid && curr_tid != idle_thread->tid)
    {
      int temp1 = INT_MULTIPLY(load_avg, 2);
      int temp2 = DIVIDE(temp1, INT_ADD(temp1, 1));
      int temp3 = MULTIPLY(temp2, curr->recent_cpu);
      curr->recent_cpu = INT_ADD(temp3, curr->nice);
    }
  }
}

void thread_update_priority(struct thread *t)
{
  tid_t curr_tid = t->tid;
  if (curr_tid != wakeup_thread->tid && curr_tid != mlfqs_thread->tid && curr_tid != idle_thread->tid)
  {
    int temp1 = INT_ADD(INT_DIVIDE(t->recent_cpu, 4), 2 * t->nice);
    int temp2 = ROUND_ZERO(INT_SUB(PRI_MAX, temp1));
    t->priority = CLAMP(temp2, PRI_MIN, PRI_MAX);
  }

  if (t->status == THREAD_READY)
  {
    enum intr_level old_level = intr_disable();
    list_remove(&t->mlfqselem);
    list_push_back(&mlfqs_lists[t->priority], &t->mlfqselem);
    intr_set_level(old_level);
  }
}

void thread_update_priorities()
{
  is_mlfqs_priority_update = false;

  struct list_elem *iter;
  for (iter = list_begin(&all_list); iter != list_end(&all_list); iter = list_next(iter))
  {
    struct thread *curr = list_entry(iter, struct thread, allelem);
    thread_update_priority(curr);
  }
}

/* Fetches the priority such that the MLFQ list corresponding to that
  priority is not empty. If no such list is found, return -1, indicating
  a situation where no thread is present to be scheduled
*/
int thread_mlfqs_get_highest_priority(void)
{
  int i;
  for (i = PRI_MAX; i >= PRI_MIN; i--)
  {
    struct list *cur_list = &mlfqs_lists[i];
    if (!list_empty(cur_list))
      return i;
  }

  return -1;
}

static bool thread_compare_priorities(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);

  if (t1->priority > t2->priority)
    return true;

  return false;
}
/* end ASSGN4 */