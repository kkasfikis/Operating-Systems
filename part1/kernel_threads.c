
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_cc.h"//remove this
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */
/** 
  @brief Create a new thread in the current process.

  The new thread is executed in the same process as 
  the calling thread. If this thread returns from function
  task, the return value is used as an argument to 
  `ThreadExit`.

  The new thread is created by executing function `task`,
  with the arguments of `argl` and `args` passed to it.
  Note that, unlike `Exec`, where argl and args must define
  a byte buffer, here there is no such requirement! 
  The two arguments are passed to the new thread verbatim,
  and can be unrelated. It is the responsibility of the
  programmer to define their meaning.

  @param task a function to execute

  */


void start_any_thread(){
	PCB* pcb = CURPROC;
	PTCB* ptcb;
	int exitval;
	rlnode* helper = rlist_pop_front(& pcb->argsTable);
	ARGST* argst = helper->args;
	ptcb=argst->ptcb;	
	Task task=argst->task;

	exitval = task(argst->argl,argst->args);
	ptcb->exitval=exitval;		
	ThreadExit(exitval);

}



Tid_t CreateThread(Task task, int argl, void* args)
{

	PCB* pcb = CURPROC;
	TCB* tcb = NULL;
	ARGST* argst;
	PTCB* ptcb = NULL;
	argst= (ARGST*)xmalloc(sizeof(ARGST));

	rlnode_init(& argst->args_node, argst);
	if(task!=NULL){
		(argst)->task = task;
		(argst)->argl = argl;
		if(args!=NULL){
			(argst)->args = args;
		}
		else{
			(argst)->args=NULL;
		}
		

		


		ptcb = acquire_PTCB(pcb);
		ptcb = initialize_PTCB(ptcb,NULL,pcb->ptcb_id);

		argst->ptcb = ptcb;
		rlist_push_back(& pcb->argsTable, &argst->args_node);

    tcb = spawn_thread(pcb,start_any_thread);		
		ptcb->m_thread = tcb;
		tcb->ptcb=ptcb; 		
		pushPTCB(pcb,ptcb);
    wakeup(tcb);
		return (Tid_t)tcb;
  }
	else{
		return NOTHREAD;	
	}

}

/**
  @brief Return the Tid of the current thread.
 */
/**
  @brief Return the Tid of the current thread.
 */
Tid_t ThreadSelf()
{
	return (Tid_t) CURTHREAD;
}

/**
  @brief Join the given thread.
  */
/**
  @brief Join the given thread.

  This function will wait for the thread with the given
  tid to exit, and return its exit status in `*exitval`.
  The tid must refer to a legal thread owned by the same
  process that owns the caller. Also, the thread must 
  be undetached, or an error is returned.

  After a call to join succeeds, subsequent calls will fail
  (unless tid was re-cycled to a new thread). 

  It is possible that multiple threads try to join the
  same thread. If these threads block, then all must return the
  exit status correctly.

  @param tid the thread to join
  @param exitval a location where to store the exit value of the joined 
              thread. If NULL, the exit statuter2$ 
s is not returned.
  @returns 0 on success and -1 on error. Possible errors are:
    - there is no thread with the given tid in this process. CHECK
    - the tid corresponds to the current thread. CHECK
    - the tid corresponds to a detached thread.	

  */

int ThreadJoin(Tid_t tid, int* exitval)
{

	PCB* pcb = CURPROC;
	PTCB* ptcb = NULL;	
	TCB* tcb=NULL;

	Mutex_Lock(& kernel_mutex);
  /* Legality checks */
	///////////////////////////////////////////	VALIDATION /////////////////////////////////////////
	//find if this thread exists in the current process
	ptcb = get_ptcb(tid,pcb);
	ptcb->waiting_for_me ++ ;
	tcb = ptcb->m_thread;
  if((tid<0)||(Tid_t)CURTHREAD==tid||ptcb==NULL||ptcb->detach==1) {    
		Mutex_Unlock(& kernel_mutex);
    return -1;
  }
	///////////////////////////////////////////  END	VALIDATION //////////////////////////////////
	if(tcb!=NULL){	
		if(tcb->state !=EXITED){
			Cond_Wait(& kernel_mutex, & ptcb->thread_exit);	
		} 
		//int x=0;
		exitval = &(ptcb->exitval);
		Mutex_Unlock(& kernel_mutex);		
  	return 0;
	}
	else{
    return -1;
	}
}

/**
  @brief Detach the given thread.
  */
/**
  @brief Detach the given thread.

  This function makes the thread tid a detached thread.
  A detached thread is not joinable (ThreadJoin returns an
  error). 

  Once a thread has exited, it cannot be detached. A thread
  can detach itself.

  @param tid the tid of the thread to detach
  @returns 0 on success, and -1 on error. Possibe errors are:
    - there is no thread with the given tid in this process.
    - the tid corresponds to an exited thread.
  */
int ThreadDetach(Tid_t tid)
{
	PCB* pcb = CURPROC;
	TCB* tcb = get_ptcb(tid,pcb)->m_thread;
	if(tcb == NULL || tcb->state ==EXITED){
		return -1;	
	}
	else{
		get_ptcb((Tid_t)tcb,pcb)->detach=1;

		return 0;	
	}
}

/**
  @brief Terminate the current thread.
  */

void ThreadExit(int exitval)
{

	TCB* tcb = CURTHREAD;
	PCB* pcb = CURPROC;	
	PTCB* ptcb = get_ptcb((Tid_t)tcb,pcb);

	if(ptcb->waiting_for_me>0){	
		Cond_Broadcast(&ptcb->thread_exit);
	}	
	Mutex_Lock(&kernel_mutex);

	sleep_releasing(EXITED,& kernel_mutex);
	
	Mutex_Unlock(&kernel_mutex);	
	
}


/**
  @brief Awaken the thread, if it is sleeping.

  This call will set the interrupt flag of the
  thread.

  */
int ThreadInterrupt(Tid_t tid)
{
	return -1;
}


/**
  @brief Return the interrupt flag of the 
  current thread.
  */
int ThreadIsInterrupted()
{
	return 0;
}

/**
  @brief Clear the interrupt flag of the
  current thread.
  */
void ThreadClearInterrupt()
{

}

