#include "tinyos.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
#include "kernel_proc.h"


file_ops SocketOpsListener = {
	.Open = NULL,
	.Read = socket_read,
	.Write =  socket_write,
	.Close = socket_close	
	};

file_ops SocketOpsPeer = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};


Fid_t Socket(port_t port) 
{	
	Mutex_Lock(&kernel_mutex);
	
	if(port<NOPORT || port>MAX_PORT){
		Mutex_Unlock(&kernel_mutex);
		return NOFILE;	
	}
		
	Fid_t myfid;
	FCB* myfcb;
	SOCB* socket = NULL;
	
	myfcb = socketFCB_reserve(&myfid); 

	if(myfcb == NULL ){		
		Mutex_Unlock(&kernel_mutex);			
		return NOFILE;
	}

	socket = (SOCB*)xmalloc(sizeof(SOCB));	
	socket->port = port;
	socket->type = UNBOUND;
	socket->fid = myfid;
	socket->refcount = 0;
	socket->shutdown_code = -1;
	socket->replicate_to = NULL;
	socket->pid = GetPid();
	socket->socketVar = COND_INIT;
	rlnode_init(&socket->queue, NULL);
	rlnode_init(&socket->node, myfcb);	
	

	myfcb->streamobj = socket;
	Mutex_Unlock(&kernel_mutex);	
	return myfid;
}

int Listen(Fid_t sock)
{
	Mutex_Lock(&kernel_mutex);
	if(sock < 0 || sock >= MAX_FILEID)
	{
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	FCB* fcb = get_fcb(sock);
	SOCB* socket = (SOCB*)fcb->streamobj;
	if(socket==NULL){
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	port_t port = socket->port;

	if(socket->port == NOPORT || socket->type == LISTENER){
		Mutex_Unlock(&kernel_mutex);
		return -1;	
	}
	port_s* port_str;
	for(int i=1;i<=MAX_PORT;i++){
		if(PortTable[i].port == port){
			if(PortTable[i].type == NOTBOUND){
				port_str = &PortTable[i];
				PortTable[i].type = BOUND;	
			}
			else{
				PCB* pcb = get_pcb(PortTable[i].listener->pid);
				FCB* temp = pcb->FIDT[PortTable[i].listener->fid];				
				if( temp->check == -1){
					Mutex_Unlock(&kernel_mutex);
					return -1;
				}
				else{
					port_str = &PortTable[i];
					PortTable[i].type = BOUND;					
				}
			}		
		}
	}
	socket->type = LISTENER;
	socket->socketVar = COND_INIT;
	port_str->listener = socket;
	Mutex_Unlock(&kernel_mutex);
	return 0;	
}


Fid_t Accept(Fid_t lsock)
{
	//	- the available file ids for the process are exhausted
	//	- while waiting, the listening socket @c lsock was closed
	Mutex_Lock(&kernel_mutex);
	if(lsock<0 || lsock>=MAX_FILEID)
	{
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	FCB* fcb = get_fcb(lsock);
	SOCB* lsocket = (SOCB*)fcb->streamobj;	
	if(lsocket==NULL){
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	if(lsocket->type != LISTENER){
		Mutex_Unlock(&kernel_mutex);
		return -1;	
	}

//=======================================================
	pipe_t pipe;
	if(Pipe(&pipe)==-1){
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	socket->sender->read = get_fcb(pipe.read);
	socket->sender->write = get_fcb(pipe.write);
	if(Pipe(&pipe)==-1){
		Mutex_Unlock(&kernel_mutex);	
		return -1;
	}
	socket->receiver->read = get_fcb(pipe.read);
	socket->receiver->write = get_fcb(pipe.write);
//=======================================================
	if(fcb==NULL){
		Mutex_Unlock(&kernel_mutex);
		return -1;	
	}

	Fid_t socketfid_t;	
	SOCB* peerlistener;
	if(lsocket->replicate_to ==NULL){
		Mutex_Unlock(&kernel_mutex);
		socketfid_t = Socket(lsocket->port);
		Mutex_Lock(&kernel_mutex);	
	  	peerlistener = (SOCB*)get_fcb(socketfid_t)->streamobj;
		peerlistener->type = LISTENERPEER;
	}
	else{	
		socketfid_t = lsocket->replicate_to->fid;
		peerlistener = lsocket->replicate_to;
	} 

	if(is_rlist_empty(&lsocket->queue)){
		Cond_Wait(&kernel_mutex,&(peerlistener->socketVar));	
	}
	
	if(!is_rlist_empty(&lsocket->queue)){
		SOCB* new_connection = (SOCB*)((FCB*)(rlist_pop_front(&lsocket->queue)->pcb))->streamobj;
		peerlistener->connected_to = new_connection;
		new_connection->connected_to = peerlistener;
	}
	Mutex_Unlock(&kernel_mutex);

	return socketfid_t;
}


int Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	Mutex_Lock(&kernel_mutex);

	FCB* fcb = get_fcb(sock);
	SOCB* peer = (SOCB*)fcb->streamobj;

	if(sock<0 || sock>=MAX_FILEID)
	{
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}	
	if(peer==NULL){
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	
	port_s* port_str;

	for(int i=0;i<MAX_PORT;i++){
		if(PortTable[i].port == port){
			if(PortTable[i].type == BOUND){
				port_str = &PortTable[i];			
				if(PortTable[i].listener->type != LISTENER)
				{
					Mutex_Unlock(&kernel_mutex);
					return -1;		
				}			
			}
			else{
				Mutex_Unlock(&kernel_mutex);
				return -1;			
			}		
		}	
	}
	SOCB* listener = port_str->listener;
	peer->type = PEER;
	rlist_push_back(& listener->queue,&peer->node);
	Cond_Signal(&listener->replicate_to->socketVar);
	//peer->connected_to = port_str->listener->queue
	Mutex_Unlock(&kernel_mutex);
	return 0;
}

/**
   @brief Shut down one direction of socket communication.

   With a socket which is connected to another socket, this call will 
   shut down one or the other direction of communication. The shut down
   of a direction has implications similar to those of a pipe's end shutdown.
   More specifically, assume that this end is socket A, connected to socket
   B at the other end. Then,

   - if `ShutDown(A, SHUTDOWN_READ)` is called, any attempt to call `Write(B,...)`
     will fail with a code of -1.
   - if ShutDown(A, SHUTDOWN_WRITE)` is called, any attempt to call `Read(B,...)`
     will first exhaust the buffered data and then will return 0.
   - if ShutDown(A, SHUTDOWN_BOTH)` is called, it is equivalent to shutting down
     both read and write.

   After shutdown of socket A, the corresponding operation `Read(A,...)` or `Write(A,...)`
   will return -1.

   Shutting down multiple times is not an error.
   
   @param sock the file ID of the socket to shut down.
   @param how the type of shutdown requested
   @returns 0 on success and -1 on error. Possible reasons for error:
       - the file id @c sock is not legal (a connected socket stream).
*/
int ShutDown(Fid_t sock, shutdown_mode how)
{
	Mutex_Lock(&kernel_mutex);
	if(sock<0 || sock >= MAX_FILEID){
		Mutex_Unlock(&kernel_mutex);		
		return -1;
	}
	FCB* fcb = get_fcb(sock);
	SOCB* socket = (SOCB*)fcb->streamobj;
	if(fcb ==NULL || socket->type == UNBOUND){
		Mutex_Unlock(&kernel_mutex);
		return  -1;	
	}
	switch(how){
		case 1:
			socket->shutdown_code = 1;
			break;
		case 2:
			socket->shutdown_code = 2;
			break;
		case 3:
			socket->shutdown_code = 3;
			release_FCB(socket->sender->read);	
			release_FCB(socket->sender->write);
			release_FCB(socket->receiver->read);	
			release_FCB(socket->receiver->write);			
			free(socket);
			break;
	}
	Mutex_Unlock(&kernel_mutex);
	return -1;
}

//==================================================================================================================
//==================================================================================================================


int socket_illegal_call_reader(void* this,char* buf,uint size){
	return -1;
}
int socket_illegal_call_writer(void* this,const char* buf,uint size){
	return -1;
}
int socket_close(void* pipecb){
	return -1;
}

int socket_read(void* pipecb, char* buf, uint size){
	return -1;
}

int socket_write(void* pipecb,const char* buf,uint size){
	return -1;
}


