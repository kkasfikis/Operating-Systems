
#include "util.h"

void raise_exception(exception_context context)
{
	if(*context) {
		__atomic_signal_fence(__ATOMIC_SEQ_CST);
		longjmp((*context)->jbuf, 1);
	}
}


void exception_unwind(exception_context context, int errcode)
{
	/* Get the top frame */
	struct exception_stack_frame* frame = *context;

	/* handle exception */
	int captured = 0;

	/* First execute catchers one by one */
	while(frame->catchers) {
		captured = 1;
		struct exception_handler_frame *c = frame->catchers;
		/* Pop it from the list, just in case it throws() */
		frame->catchers = c->next;
		c->handler(errcode);
	}

	/* Execute finalizers one by one */
	while(frame->finalizers) {
		struct exception_handler_frame *fin = frame->finalizers;
		frame->finalizers = fin->next;

		fin->handler(errcode);
	}
 	
	/* pop this frame */
	*context = frame->next;

 	/* propagate */
	if(errcode && !captured) 
		raise_exception(context);
}

int isEmpty(io_buffer *buffer){
	if(buffer->space.available == IO_BUFFER_CAPACITY){
		return 1;
	}
	else{
		return 0;
	}
}
int isFull(io_buffer *buffer){
	if(buffer->data.available == IO_BUFFER_CAPACITY){
		return 1;	
	}
	else{
		return 0;
	}
}

uint io_buffer_segment_reserve(io_buffer_segment* this, uint size, uint* pos)
{

	io_buffer_segment expected, desired;
	uint reserved;

	expected = *this;

	do {
		reserved = (expected.available) < size ? expected.available : size;

		desired.position = (expected.position + reserved) % IO_BUFFER_CAPACITY;
		desired.available = expected.available - reserved;

	} while(! __atomic_compare_exchange(this, &expected, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));

	*pos = expected.position;
	return reserved;
}


void io_buffer_segment_unreserve(io_buffer_segment* this, uint size)
{
	io_buffer_segment expected, desired;

	expected = *this;

	do {
		desired.position = (expected.position+(IO_BUFFER_CAPACITY-size)) % IO_BUFFER_CAPACITY;
		desired.available = expected.available + size;

	} while(! __atomic_compare_exchange(this, &expected, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
}


/*
	This operation increases this->available by size.
	The operation is atomic and lock-free.

	The previous value of this->available is returned.
 */
uint io_buffer_segment_release(io_buffer_segment* this, uint size)
{
	io_buffer_segment expected, desired;

	expected = *this;

	do {
		desired.position = expected.position;
		desired.available = expected.available + size;
	} while(! __atomic_compare_exchange(this, &expected, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));

	return expected.available;
}



void io_buffer_init(io_buffer* this)
{
	this->data  = (io_buffer_segment) { 0, 0 };
	this->space = (io_buffer_segment) { 0, IO_BUFFER_CAPACITY };
}


/* helper */ 
static inline uint contiguous(uint pos, uint size) 
{ 
	if(pos+size <= IO_BUFFER_CAPACITY) 
		return size;
	else
		return IO_BUFFER_CAPACITY - pos;
}


void io_buffer_get(io_buffer* this, uint pos, char* buf, uint size)
{
	while(size>0) {
		uint csize = contiguous(pos, size);
		memcpy(buf, this->storage + pos, csize);
		pos = (pos+csize) % IO_BUFFER_CAPACITY;
		size -= csize;
		buf += csize;
	}
}


void io_buffer_put(io_buffer* this, uint pos, char* buf, uint size)
{
	while(size>0) {
		uint csize = contiguous(pos, size);
		memcpy(this->storage + pos, buf, csize);
		pos = (pos+csize) % IO_BUFFER_CAPACITY;
		size -= csize;
		buf += csize;
	}	
}


uint io_buffer_read(io_buffer* this, char* buf, uint bufsize,uint pointer)
{
	uint size = io_buffer_segment_reserve(&this->data, bufsize, &pointer);

	io_buffer_get(this, pointer, buf, size);

	io_buffer_segment_release(&this->space, size);
	return size;
}



uint io_buffer_write(io_buffer* this,const char* buf, uint bufsize,uint pointer)
{
	uint size = io_buffer_segment_reserve(&this->space, bufsize, &pointer);

	io_buffer_put(this, pointer, buf, size);

	io_buffer_segment_release(&this->data, size);
	return size;
}


