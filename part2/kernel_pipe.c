
#include "tinyos.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
file_ops ReaderOps = {
	.Open = NULL,
	.Write = pipe_illegal_call_reader,
	.Read =  pipe_read,
	.Close = pipe_close	
	};
file_ops WriterOps = {
	.Open = NULL,
	.Read = pipe_illegal_call_writer,
	.Write =  pipe_write,
	.Close = pipe_close	
	};
int Pipe(pipe_t* pipe)
{
	Mutex_Lock(&kernel_mutex);
	PICB* myPipe = NULL;
	Fid_t myArray[2];
	//myArray[0] = pipe->read;
	//myArray[1] = pipe->write;
	FCB* myFCBs[2];
	//myFCBs[0] = get_fcb(myArray[0]);
	//myFCBs[1] = get_fcb(myArray[1]);
	int check = FCB_reserve(2, myArray,myFCBs);
		
	if(!check)
	{
		Mutex_Unlock(&kernel_mutex);
		return -1;      
	}	 

	myPipe = (PICB*)xmalloc(sizeof(PICB));
	myPipe->buffer = (io_buffer*)xmalloc(sizeof(io_buffer));
	io_buffer_init(myPipe->buffer);

	myFCBs[0]-> streamobj = myPipe;
	myFCBs[1]-> streamobj = myPipe;
	myFCBs[0] ->streamfunc = &ReaderOps;
	myFCBs[1]->streamfunc = &WriterOps;
	myPipe->read = myFCBs[0];
	myPipe->write = myFCBs[1];
	myPipe->reader_pointer = 0;
	myPipe->writer_pointer = 0;
	myPipe->reader_var = COND_INIT;
	myPipe->writer_var = COND_INIT;
	pipe->read = myArray[0];
	pipe->write = myArray[1];
	Mutex_Unlock(&kernel_mutex);
	return 0;
}


//==================================================================================================================
//==================================================================================================================


int pipe_illegal_call_reader(void* this,char* buf,uint size){
	return -1;
}
int pipe_illegal_call_writer(void* this,const char* buf,uint size){
	return -1;
}
int pipe_close(void* pipecb){
	PICB * pipe = (PICB*)pipecb;
	//reader and writer refcount 0 --> then close;	
	if(pipe->read->refcount ==0 && pipe->write->refcount ==0){
		release_FCB(pipe->read);
		release_FCB(pipe->write);
		free(pipe);	
	}
	return 0;
}

int pipe_read(void* pipecb, char* buf, uint size){
	int ret;
	PICB * pipe = (PICB*)pipecb;	
	if(pipe->write->refcount ==0)
	{
		if(isEmpty(pipe->buffer)){
			pipe->read->refcount =0;
			return 0;
		}
	}
	while(isEmpty(pipe->buffer) && pipe->write->refcount!=0)
	{
		Cond_Signal(&(pipe->writer_var));
		Cond_Wait(&kernel_mutex,&(pipe->reader_var));
	}
	Cond_Broadcast(&(pipe->writer_var));
	ret=(int)io_buffer_read(pipe->buffer,buf,size,pipe->reader_pointer);
	return ret;
}

int pipe_write(void* pipecb,const char* buf,uint size){
	int ret;
	PICB * pipe = (PICB*)pipecb;
	if(pipe->read->refcount ==0)
	{
		return -1;	
	}	
	while(isFull(pipe->buffer) && pipe->read->refcount!=0)
	{
		Cond_Signal(&(pipe->reader_var));
		Cond_Wait(&kernel_mutex,&(pipe->writer_var));
	}
	Cond_Broadcast(&(pipe->reader_var));
	ret=(int)io_buffer_write(pipe->buffer,buf,size,pipe->writer_pointer);
	return ret;
}



