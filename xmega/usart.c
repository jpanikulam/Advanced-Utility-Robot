/* program definitions for USART handling functions */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include "types.h"
#include "message.h"
#include "meta.h"
#include "usart.h"

int usart_busy_flag = 0;  //define the global flag
Buffer* in_buffer;        //define the buffers
Buffer* out_buffer;
static int error = OK;    //flag for error handling

//static variables for resolver (needed to prevent blocking)
static Message m_in;     //message storage
static Message m_out;    //output message cache
static int out_count;     //outgoing byte count
static int in_count;      //incoming byte count

//Function to configure USART
void initialize_usart(){
  //setup TX (pin 3) to output and set default value to 1
  PORTC.DIR = PIN3_bm;
  PORTC.OUT = PIN3_bm;
	
  //Store the values for BSEL (A[7-0] and B[3-0]) and BSCALE(B[7-4])
  USARTC0.BAUDCTRLA = (BSEL & 0xFF);
  USARTC0.BAUDCTRLB = ((BSCALE << 4) & 0xF0) | ((BSEL >> 8) & 0x0F);

  // USART is Asynchronous, no parity, 1 stop bit, 8-bit mode (00-00-0-011)
  USARTC0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	
  //configure the usart to generate interrupts on recieve (high) and data ready (mid)
  USARTC0.CTRLA = USART_RXCINTLVL_HI_gc || USART_DREINTLVL_MED_gc;	//0x32

  //Finally, Turn on TX (bit 3) and RX (bit 4)
  USARTC0.CTRLB = PIN3_bm | PIN4_bm;
  
  //initialize the buffers (calloc makes everything 0)
  in_buffer = (Buffer*)calloc(1,sizeof(Buffer));
  out_buffer = (Buffer*)calloc(1,sizeof(Buffer));;

  //initialize flags/counts/pointers
  usart_busy_flag = 0;  //usart is not busy
  in_count = 0;         //no data yet
  out_count = 0;
}

//add data to the start of the buffer
int buffer_push(Buffer* b, uint8_t data){
  if((b->end+1)%MAX_BUFFER_LENGTH == b->start) return BUFFER_ERROR_TYPE;
  b->data[b->end] = data;  //add to end of list
  b->end = (b->end+1)%MAX_BUFFER_LENGTH;  //increment end index
  return OK;
}

//remove data from start of buffer
int buffer_pop(Buffer* b, uint8_t* data){
  if(b->end == b->start) return BUFFER_ERROR_TYPE;
  *data = b->data[b->start]; //get the first element
  b->start = (b->start+1)%MAX_BUFFER_LENGTH;  //increment start index
  return OK;
}

//wipe all pending buffer data
void wipe_in_buffer(){
  in_buffer->start = in_buffer->end;  //clear the queue
  in_count = 0; //stop processing current message
}

void wipe_out_buffer(){
  out_buffer->start = out_buffer->end;  //clear queue
  out_count = 0;  //stop processing the message
}

/* function will alternate between buffers until both are full/empty, or until 
 * a specified number of bytes. Will also handle error flags from interrupts.
 * Should be run often. */
void resolve_buffers(int bytes){
  int turn = IN_QUEUE;  //keep track of who's turn it is

  //loop while there's work available and we're under the byte cap (timesharing)
  //loop while: (in buffer is not empty || 
  //  (out buffer is not full && (there is an outgoing message || we're processing one) 
  //  && (we've started sending)) && we're still allowed to)
  while((in_buffer->start != in_buffer->end || \
    ((out_buffer->start != (out_buffer->end+1)%MAX_BUFFER_LENGTH)\
    && (out_queue || out_count) && start_ok)) && bytes > 0){
    //go until in buffer is empty and output buffer is full or there are no more messages
    //decide who's turn it is (alternate unless somebody is full/empty)
    if(in_buffer->start == in_buffer->end) turn = OUT_QUEUE; //determine if somebody isn't allowed to go
    else if((out_buffer->start == (out_buffer->end+1)%MAX_BUFFER_LENGTH) || !out_queue || !start_ok) turn  = IN_QUEUE;
    else turn = (turn+1)%2; //otherwise alternate

    //throw to individualized methods
    if(turn == IN_QUEUE) resolve_single_input();
    else  resolve_single_output();
    
    //handle errors generated by interrupts (really just buffer full)
    if(error != OK){  //check error status
      Message m; //create a message
      m.type = (uint8_t) error; //set the type to the error
      m.size = 0;
      queue_push(m,OUT_QUEUE); //report errors to host computer
      error = OK; //reset the flag
    }
    bytes--;
  }
}

/* function will resolve a single input byte from the buffer */
void resolve_single_input(){
  int offset = -3;
  int done = 0;
  uint8_t data;
    
  buffer_pop(in_buffer, &data); //read a byte from the buffer
  
  //preform different actions based on the byte count
  if(0 == in_count){  //type field
    m_in.type = data; //get the data
    m_in.data = 0;    //null pointer
    if((m_in.type & DATA_MASK ) == NO_DATA_TYPE){  //determine the size
      m_in.size = 0;
    } else if((m_in.type & DATA_MASK ) == DATA_1B_TYPE){
      m_in.size = 1;
      m_in.data = (uint8_t*) malloc(1);  //allocate 1B for data
    } else if((m_in.type & DATA_MASK ) == DATA_2B_TYPE){
      m_in.size = 2;
      m_in.data = (uint8_t*) malloc(2);  //allocate 2B for data
    } else if((m_in.type & DATA_MASK ) == DATA_NB_TYPE) m_in.size = 2;  //size is not here yet
  } else if (1 == in_count && ((m_in.type & DATA_MASK ) == DATA_NB_TYPE)){
    m_in.size = data;  //get the size
    m_in.data = (uint8_t*) malloc(m_in.size); //allocate the requested amount of data
  } else {  //byte is data
    offset = ((m_in.type & DATA_MASK ) == DATA_NB_TYPE)? in_count - 2: in_count - 1;
    *((m_in.data)+offset) = data;  //store the byte
    if(((m_in.type & DATA_MASK ) == DATA_NB_TYPE)? offset+2: offset+1 == m_in.size) done = 1;  //check if done
  }
  if(((m_in.type & DATA_MASK) == NO_DATA_TYPE) || done){  //check if we've got the whole message
    if(OK != queue_push(m_in,IN_QUEUE))  //try to push the incoming message to the queue
      error = MESSAGE_ERROR_TYPE; //report the error on failure
    in_count = 0; //reset the counter
  } else in_count++; //new byte, increment count
}

/* function will place a single output byte in the buffer */
void resolve_single_output(){
  uint8_t data; //the data to buffer
  int offset = -2;   //offset into the data field
  
  if(0 == out_count){ //new message, send type
    if(OK != queue_pop(&m_out,OUT_QUEUE)){ //get the next message in the queue
      out_count = 0;  //no message, shut the whole thing down
      return; 
    }
    data = m_out.type;  //send the type field
    if((m_out.type & DATA_MASK ) != DATA_NB_TYPE) m_out.size = m_out.type >> 6; //make sure size is correct
  } else if(((m_out.type & DATA_MASK ) == DATA_NB_TYPE) && out_count == 1){
    data = m_out.size; //send the size
  } else { //send the data
    offset = ((m_out.type & DATA_MASK ) == DATA_NB_TYPE)? out_count - 2 : out_count - 1;
    data = *(m_out.data + offset);
  }
    
  buffer_push(out_buffer, data);  //place the data in the buffer
  ++out_count;  //incremet this message's sent byte count
  
  if(!usart_busy_flag){ //check if interrupt is disabled
    usart_busy_flag = 1;  //usart is now busy
    USARTC0.CTRLA = USART_RXCINTLVL_HI_gc || USART_DREINTLVL_MED_gc;	//enable the interrupt
  }
  
  if(((m_out.type & DATA_MASK) == NO_DATA_TYPE) || offset+1 == m_out.size){  //check if message is done
    if((m_out.type & DATA_MASK) != NO_DATA_TYPE) free(m_out.data);  //free the buffer
    out_count = 0;  //clear the count
  }
}

ISR(USARTC0_RXC_vect){
  int status = buffer_push(in_buffer,USARTC0.DATA);
  if(status != OK) error = status; //report error (full buffer)
}

ISR(USARTC0_DRE_vect){
  uint8_t data;	//have to pull the data first, won't write to address
  int status = buffer_pop(out_buffer,&data);
  if(status != OK){ //shutdown if buffer is empty
    usart_busy_flag = 0;  //usart is now not busy
    USARTC0.CTRLA = USART_RXCINTLVL_HI_gc;	//disable the DRE interrupt
  } else {
	  USARTC0.DATA = data;	//write the output if read was good
  }
}
