#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define AGE 20

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// for MLFQ
int q_size[5] = {-1, -1, -1, -1, -1};   // gives no of processes in each queue (0 index-based)
int q_ticks_max[5] = {1, 2, 4, 8, 16};  // max time slice in each queue

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  /* initialize variables for waitx */
  p->ctime = ticks;
  p->rtime = 0;
  p->etime = 0;
  p->wtime = 0;
  /* Default priority */
  p->priority = 60;

  /* MLFQ var initialization */
  for (int i = 0; i < 5; i++)
    p->ticks[i]=0;
  p->n_run = 0;
  p->curr_queue = 0;
  p->curr_ticks = 0;
  p->enter = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  #ifdef MLFQ
  shift_proc_q(p,-1,0); // add proc to queue 0
  #endif

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  #ifdef MLFQ
  shift_proc_q(np,-1,0);
  #endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  curproc->etime = ticks;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        #ifdef MLFQ
        shift_proc_q(p,p->curr_queue,-1); // remove proc from curr_queue
        #endif
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        #ifdef MLFQ
        shift_proc_q(p,p->curr_queue,-1); // remove proc from curr_queue
        #endif
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        /* ONLY DIFF THAN wait */
        *rtime = p->rtime;
        p->etime = ticks;
        *wtime = p->etime - p->ctime - p->rtime;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p=0;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  #ifdef RR
    for(;;){
      // Enable interrupts on this processor.
      sti();

      // Loop over process table looking for process to run.
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&ptable.lock);
    }
  #else
  #ifdef FCFS
    for(;;){
      // Enable interrupts on this processor.
      sti();
      struct proc* min_time_proc=0; //process with min creation time

      // Loop over process table looking for process to run.
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;

        if (min_time_proc==0) 
          min_time_proc = p;
        else if (min_time_proc->ctime > p->ctime) 
          min_time_proc = p;
      }

      if (min_time_proc!=0)
      {
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = min_time_proc;
        switchuvm(min_time_proc);
        min_time_proc->state = RUNNING;

        swtch(&(c->scheduler), min_time_proc->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&ptable.lock);
    }
  #else
  #ifdef PBS
    for(;;){
      // Enable interrupts on this processor.
      sti();

      struct proc* min_priority_proc = 0;
      // Loop over process table looking for process to run.
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if(p->state != RUNNABLE)
          continue;

        min_priority_proc = p;
        // extra loop because for same priority process round robin
        for (struct proc *itr = ptable.proc; itr < &ptable.proc[NPROC]; itr++)
        {
          if(itr->state != RUNNABLE) 
            continue;
          if(itr->priority < min_priority_proc->priority)
            min_priority_proc = itr;
        }
        if (min_priority_proc != 0)
        {
          p = min_priority_proc;
          // Switch to chosen process.  It is the process's job
          // to release ptable.lock and then reacquire it
          // before jumping back to us.
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
      }
      release(&ptable.lock);
    }
  #else
  #ifdef MLFQ
    for(;;){
      // Enable interrupts on this processor.
      sti();

      // Loop over process table looking for process to run.
      acquire(&ptable.lock);

      // AGING
      for (int i = 1; i <= 4; i++)
      {
        for (int j = 0; j <= q_size[i]; j++)
        {
          int age = ticks - queue[i][j]->enter;
          if (age > AGE)
            shift_proc_q(queue[i][j],i,i-1);  // shift from i queue i-1 queue
        }
      }
      
      for (int i = 0; i <= 4; i++)
      {
        if (q_size[i]>=0)
        {
          p = queue[i][0];
          shift_proc_q(p,i,-1);
          break;
        }
      }
      if(p!=0 && p->state == RUNNABLE)
      {
        // if(p->state != RUNNABLE)
        //   continue;
        // cprintf("bitch run please!\n");

        p->curr_ticks++;
        p->n_run++;
        p->ticks[p->curr_queue]++;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;

        // TIME SLICE OF PROCESS FINISHED = shift lower priority queue
        if (p!=0 && p->state == RUNNABLE)
        {
        // cprintf("bitch run please!\n");
        // cprintf("%d\n",p->pid);
          p->curr_ticks = 0;
          
          if (p->change_q == 1) 
          {
            p->change_q = 0;
            if (p->curr_queue < 4) 
            {
              shift_proc_q(p,p->curr_queue,(p->curr_queue)+1);
              p->curr_queue++;
            }
          }
          else
            shift_proc_q(p,p->curr_queue,p->curr_queue); // add process to same queue
        }
      }
      release(&ptable.lock);
    }
  #endif
  #endif
  #endif
  #endif
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      #ifdef MLFQ
      p->curr_ticks = 0;
      shift_proc_q(p,-1,p->curr_queue);
      #endif
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
      {
        p->state = RUNNABLE;
        #ifdef MLFQ
        shift_proc_q(p,-1,p->curr_queue);
        #endif
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void change_time()
{
  acquire(&ptable.lock);
	
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
  	if(p -> state == RUNNING)
    {	
      p -> rtime++;
      
      #ifdef MLFQ
      p -> ticks[p -> curr_queue]++;
      p -> curr_ticks++;     
      #endif
    }

  	else
  	{
      p->wtime++;	
      #ifdef MLFQ
      if(p -> curr_queue != 0 && ticks-p->curr_ticks > AGE) 
      {
        // cprintf("Aging for process %d\n", p -> pid);
        p -> curr_queue--;
        p -> curr_ticks = 0;
        p -> wtime = 0;
        shift_proc_q(p,-1,p->curr_queue);
      }
      #endif
    }  
  }

  release(&ptable.lock);
}

int set_priority(int new_priority, int pid)
{
  // cprintf("%d %d\n", new_priority, pid);
  acquire(&ptable.lock);
  int old_priority=-1;

  for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      old_priority = p->priority;
      p->priority = new_priority;
      break;
    }
  }
  release(&ptable.lock);
  return old_priority;
}

// priority = priority of currently running process
int checkPreempt(int priority, int samePriority)
{
  acquire(&ptable.lock);

  // Just checking if lower priority process has come into queue
  if (samePriority==0)
  {
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->pid == 0) 
        continue;
      else if (p->priority < priority) 
      {
        // process with less priority found
        release(&ptable.lock);
        return 1;
      }
    }
  }

  // time slice of running process finished
  // check if same (or less) priority process present, if not - do nothing
  // we will apply round robin for same priority processes
  else
  {
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->pid == 0) 
        continue;
      else if (p->priority <= priority)
      {
        release(&ptable.lock);
        return 1;
      }  
    }
  }
  
  release(&ptable.lock);
  return 0;
}

// system call used by ps.c to print all the active process stats
int printpinfos()
{
  acquire(&ptable.lock);
  for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == 0) 
      continue;

    char *state;

    if (p->state==0) state = "UNUSED";
    if (p->state==1) state = "EMBRYO";
    if (p->state==2) state = "SLEEPING";
    if (p->state==3) state = "RUNNABLE";
    if (p->state==4) state = "RUNNING";
    else             state = "ZOMBIE";

    cprintf(" %d\t%d\t%s\t%d\t%d\t%d\t%d  |  %d    %d    %d    %d    %d    %d\n",
    p-> pid, p->priority, state, p->rtime, p->wtime, p->n_run, p->curr_queue,
    p->ticks[0], p->ticks[1], p->ticks[2], p->ticks[3], p->ticks[4]);
  }
  release(&ptable.lock);

  return 0;
}

void change_q_flag(struct proc* p)
{
	acquire(&ptable.lock);
	p-> change_q = 1;
	release(&ptable.lock);
}

void incr_curr_ticks(struct proc *p)
{
	acquire(&ptable.lock);
	p->curr_ticks++;
	p->ticks[p->curr_queue]++;
	release(&ptable.lock);
}

// remove from q_i & add in q_f
// (p,-1,q_f) = add proc in q_f
// (p,q_i,-1) = remove proc from q_i
int shift_proc_q(struct proc *p, int q_i, int q_f) 
{
  // pop from q_i
  if (q_f==-1)
  {
    int found = -1;
    for (int i = 0; i <= q_size[q_i]; i++)
    {
      if (queue[q_i][i]->pid == p->pid)
      {
        found = i;
        break;
      }
    }
    if (found == -1) return -1;
    
    for (int i = found; i < q_size[q_i]; i++)
      queue[q_i][i] = queue[q_i][i+1];

    q_size[q_i]--;    
    return 1;
  }

  // push into q_f
  else if (q_i==-1)
  {
    for (int i = 0; i <= q_size[q_f]; i++)
    {
      if(queue[q_f][i]->pid == p->pid)
        return -1;
    }
    p->enter = ticks;
    p->curr_queue = q_f;
    q_size[q_f]++;
    queue[q_f][q_size[q_f]] = p;

    return 1;
  }

  // actual shift
  else 
  {
    int found = -1;
    for (int i = 0; i <= q_size[q_i]; i++)
    {
      if (queue[q_i][i] -> pid == p->pid)
      {
        found = i;
        break;
      }
    }
    if (found == -1) return -1;
    
    for (int i = found; i < q_size[q_i]; i++)
      queue[q_i][i] = queue[q_i][i+1];

    q_size[q_i]--;    

    for (int i = 0; i <= q_size[q_f]; i++)
    {
      if(queue[q_f][i]->pid == p->pid)
        return -1;
    }
    p->enter = ticks;
    p->curr_queue = q_f;
    q_size[q_f]++;
    queue[q_f][q_size[q_f]] = p;

    return 1;
  }
}
