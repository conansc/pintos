/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/string.h"

void producer_consumer(unsigned int num_producer, unsigned int num_consumer);
void test_producer_consumer(void);
void producer_consumer(UNUSED unsigned int, UNUSED unsigned int);
void producer(void *);
void consumer(void *);

void test_producer_consumer(void)
{
    //producer_consumer(0, 0);
    //producer_consumer(1, 0);
    //producer_consumer(0, 1);
    //producer_consumer(1, 1);
    //producer_consumer(3, 1);
    //producer_consumer(1, 3);
    //producer_consumer(4, 4);
    //producer_consumer(7, 2);
    //producer_consumer(2, 7);
    producer_consumer(6, 6);
    pass();
}

//Create buffer
unsigned int buffer_size = 3;
char buffer[3];
unsigned int buffer_counter = 0;
unsigned int buffer_next = 0;
unsigned int buffer_free = 0;
struct lock buffer_lock;
struct condition characters_available; 
struct condition space_available;

//Create string that should be produced by producer
const char* producer_content = "Hello World";

//Create variables needed for the termination of all threads
struct condition terminated;
struct lock terminated_lock;
unsigned int threads_started;
unsigned int producer_started;
unsigned int producer_terminated = 0;


void producer_consumer(UNUSED unsigned int num_producer, UNUSED unsigned int num_consumer)
{

	//Initialize locks and conditions
	lock_init(&buffer_lock);
	lock_init(&terminated_lock);
	cond_init(&characters_available);
	cond_init(&space_available);
	cond_init(&terminated);
	producer_started = num_producer;
	threads_started = num_producer + num_consumer;

	//Create producer threads 
	unsigned int i;
	char name_buffer[20];
	for(i=0; i<num_producer; i++) 
	{
		snprintf(name_buffer, 20, "producer_#%d", i);
		thread_create(name_buffer, PRI_MIN, producer, NULL);
	}
	
	//Create consumer threads
    for(i=0; i<num_consumer; i++) 
	{
		snprintf(name_buffer, 20, "consumer_#%d", i);
		thread_create(name_buffer, PRI_MIN, consumer, NULL);
	}

	//Get lock, because shared variables are accessed
	lock_acquire(&buffer_lock);
	
	//Wait for the producer to terminate
	//If all producers terminated the condition becomes false and the thread goes on
	while(producer_started != producer_terminated)
	{
		cond_wait(&terminated, &buffer_lock);
	}

	//Signalize consumer that they can go on, so they check condition again and terminate
	for(i=0;i<num_consumer;i++) 
	{
		cond_signal(&characters_available, &buffer_lock);	
	}	
	
	//Release acquired lock
	lock_release(&buffer_lock);

}

/*
 * Producer that fills list of buffer with characters of the string "Hello World"
 */
void producer(void * arg UNUSED) 
{	
	
	//Get size of string
	size_t len = strlen(producer_content);
	
	//Iterate over every character of the produced string
	unsigned int i;
	char curr_character;
	for (i = 0; i < len; i++)
	{
		
		//Get current character and create buffer element from it 
		curr_character = producer_content[i];
		//msg("Producer: Try to produce %c", curr_character);
		
		//Acquire buffer lock to prevent data races 
		lock_acquire(&buffer_lock);

		//Check if list is already full
		while (buffer_counter >= buffer_size)
		{
			//If so, wait for available space in list
			cond_wait(&space_available, &buffer_lock);
		}

		//Add characater to buffer
		buffer[buffer_free] = curr_character;	
		buffer_free++;
		buffer_free = buffer_free % buffer_size;		
		buffer_counter++;

		//Signal waiting consumer that there are produced elements in list 
		cond_signal(&characters_available, &buffer_lock);
		
		//Release lock for list
		lock_release(&buffer_lock);
		
	}

	//Tell the main thread that a further producer terminated
	//Additionally, increment the respective variable
	lock_acquire(&buffer_lock);
	producer_terminated++;
	cond_signal(&terminated, &buffer_lock);
	lock_release(&buffer_lock);


}

/*
 * Consumer that takes elements from the list in buffer and prints them out
 */
void consumer(void * arg UNUSED) 
{
	char curr_character;

	while (true) 
	{

		//Acquire buffer lock to prevent data races 
		lock_acquire(&buffer_lock);

		//Check if there are no elements in buffer list
		while (buffer_counter==0)
		{
			//Additionally check if no producer is running that can produce further characters
			if(producer_terminated==producer_started) {
				//If so, terminate, because there will not come new characters
				lock_release(&buffer_lock);
				return;
			}

			//If so, wait for a producer to put an element into buffer list
			cond_wait(&characters_available, &buffer_lock);
		}

		//Get first element from buffer
		curr_character = buffer[buffer_next];
		buffer_next++;
		buffer_next = buffer_next % buffer_size;
		buffer_counter--;

		//Signal waiting producer that there is available space in buffer list
		cond_signal(&space_available, &buffer_lock);

		//Finally, consume the acquired element from buffer list, i.e. print it out 
		printf("%s: Consumed %c\n", thread_current()->name, curr_character);

		//Realease lock for buffer list
		lock_release(&buffer_lock);
		
	}


}





