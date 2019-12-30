
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;
  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;
  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
	rlnode_new(&pcb->ptcbTable);
	rlnode_new(&pcb->argsTable);
	pcb->arguments_count=0;
	pcb->ptcb_count =0;
	pcb->ptcb_id =0;
  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }
  process_count = 0;
  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;
  if(pcb_freelist != NULL) { 
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }
  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/


void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
	
  Exit(exitval);
}
/*
	System call to create a new process.
 */
Pid_t Exec(Task call, int argl, void* args) ////////////////////////////////////////////////////EDITED////////////////////////////////////////////////
{
  PCB *curproc, *newproc;
	PTCB * ptcb;
  
  Mutex_Lock(&kernel_mutex);
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;
    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }
  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;
  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);
		/////EDITED//////
				
		ptcb = acquire_PTCB(newproc);
		ptcb = initialize_PTCB(ptcb,newproc->main_thread,newproc->ptcb_id);
		
		newproc->main_thread->ptcb=ptcb; 		
		pushPTCB(newproc,ptcb);

    wakeup((&newproc->ptcbTable)->next->ptcb->m_thread);
		/////EDITED/////
  }
finish:
  Mutex_Unlock(&kernel_mutex);
  return get_pid(newproc);
}


/* System call */
Pid_t GetPid()
{
  return get_pid(CURPROC);
}


Pid_t GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{
  Mutex_Lock(& kernel_mutex);

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    Cond_Wait(& kernel_mutex, & parent->child_exit);
  
  cleanup_zombie(child, status);
  
finish:
  Mutex_Unlock(& kernel_mutex);
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;
  Mutex_Lock(&kernel_mutex);

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  if(is_rlist_empty(& parent->children_list)) {
    cpid = NOPROC;
    goto finish;
  }

  while(is_rlist_empty(& parent->exited_list)) {
    Cond_Wait(& kernel_mutex, & parent->child_exit);
  }

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

finish:
  Mutex_Unlock(& kernel_mutex);
  return cpid;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void Exit(int exitval)  ////////////////////////////////////////////////////EDITED////////////////////////////////////////////////
{
  /* Right here, we must check that we are not the boot task. If we are, 
     we must wait until all processes exit. */
  if(GetPid()==1) {
    while(WaitChild(NOPROC,NULL)!=NOPROC);
  }

  /* Now, we exit */
  Mutex_Lock(& kernel_mutex);

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* Do all the other cleanup we want here, close files etc. */
  if(curproc->args) {

    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }
	
  /* Reparent any children of the exiting process to the 
     initial task */
  PCB* initpcb = get_pcb(1);
  while(!is_rlist_empty(& curproc->children_list)) {
    rlnode* child = rlist_pop_front(& curproc->children_list);
    child->pcb->parent = initpcb;
    rlist_push_front(& initpcb->children_list, child);
  }

  /* Add exited children to the initial task's exited list 
     and signal the initial task */
  if(!is_rlist_empty(& curproc->exited_list)) {
    rlist_append(& initpcb->exited_list, &curproc->exited_list);
    Cond_Broadcast(& initpcb->child_exit);
  }

  /* Put me into my parent's exited list */
  if(curproc->parent != NULL) {   /* Maybe this is init */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    Cond_Broadcast(& curproc->parent->child_exit);
  }
	
  /* Disconnect my main_thread */
  curproc->main_thread = NULL;
	
  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  curproc->exitval = exitval;
	/////////////////EDITED///////////////////////
	//rlnode* helper = (&curproc->ptcbTable)->next;	
	//rlnode* helper1;
	//for (int i=0;i<=curproc->ptcb_count;i++){
	//	helper1 = helper->next;
	//	free(helper);
	//	helper = helper1;
	//}
	/////////////////EDITED///////////////////////

  /* Bye-bye cruel world */
  sleep_releasing(EXITED, & kernel_mutex);
}


//////////////////////////////////////////////////////////////EDITED//////////////////////////////////////////////////////////////////////////
PTCB* get_ptcb(Tid_t tid,PCB* pcb)
{
	rlnode* helper=(&pcb->ptcbTable)->next;
	for (int i=0;i<=pcb->ptcb_count;i++){
		if((Tid_t)(helper->ptcb->m_thread) == tid){								
			break;
		}
		else{
			helper = helper -> next;
		}
	}
	if(helper->ptcb != NULL){
		return helper->ptcb;
	}
	else{
		return NULL;
	}
}

Pid_t get_ptid(PTCB* ptcb)
{
	if(ptcb == NULL){
		return NOPT;	
	}
	else{
		return ptcb->ptid;	
	}
}

void release_PTCB(PTCB* ptcb,PCB* pcb){
	rlnode* prev;
	rlnode* next;
	prev= (&ptcb->ptcb_node)->prev;
	next = (&ptcb->ptcb_node)->next;
	prev->next = next;
	next->prev =prev;
	free(ptcb);
	pcb->ptcb_count--;
}

PTCB* initialize_PTCB(PTCB* ptcb,TCB* tcb,int ptid){ 

	ptcb->exitval = -1 ;
	ptcb->waiting_for_me = 0;
	
	rlnode_init(&ptcb->ptcb_node,ptcb);
	ptcb->m_thread = tcb;
	ptcb->ptid = ptid;
	ptcb->detach = 0;
	ptcb->state_spinlock = MUTEX_INIT;
	ptcb->thread_exit = COND_INIT;

	return ptcb;
}


void pushPTCB(PCB* pcb,PTCB* ptcb){

	rlist_push_back(&pcb->ptcbTable, &ptcb->ptcb_node);

}

void popSpecPTCB (PCB* pcb,int ptid){
	rlnode* helper = (&pcb->ptcbTable)->next;
	for (int i=0;i<=pcb->ptcb_count;i++){
		if(helper->ptcb->ptid == ptid){
			release_PTCB(helper->ptcb,pcb);
			break;
		}
		else{
			helper = helper->next;
		}
	}
}

void popFrontPTCB(PCB* pcb){
	rlist_pop_front(&pcb->ptcbTable);
}

PTCB* acquire_PTCB(PCB* pcb){

	PTCB* ptcb;	
	ptcb=(PTCB*)xmalloc(sizeof(PTCB));

	ptcb->ptid = pcb->ptcb_count;
	pcb->ptcb_count++;
	pcb->ptcb_id++;

	return ptcb;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Fid_t OpenInfo()
{
	return NOFILE;
}

