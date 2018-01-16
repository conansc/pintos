/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <random.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "devices/timer.h"

/* Contains all shared state variables and all shared semaphores */
struct buff
{
	struct semaphore mutex;
	struct semaphore bridge_left_normal;
	struct semaphore bridge_right_normal;
	struct semaphore bridge_left_emergency;
	struct semaphore bridge_right_emergency;

	unsigned int active_left;
	unsigned int active_right;
	unsigned int waiting_left_normal;
	unsigned int waiting_right_normal;
	unsigned int waiting_left_emergency;
	unsigned int waiting_right_emergency;
};

/* Contains the thread specific data (direction, priority) for each thread */
struct data
{
	unsigned int direc;
	unsigned int prio;
};	

/* Prototypes */
void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);
void one_vehicle (void*);
void arrive_bridge(unsigned int, unsigned int);
void cross_bridge(unsigned int, unsigned int);
void exit_bridge(unsigned int);
void data_init(struct data*, unsigned int, unsigned int);
void buff_init(struct buff*);
void print_stats(struct buff*);

struct buff buff;				/* Shared Buffer */
struct semaphore ok_to_finish;	/* Semaphore checking, if it's ok to finish narrow bridge */

void test_narrow_bridge(void)
{
    /*narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);*/
    narrow_bridge(0, 10, 10, 0);
    pass();
}

/* Initializes the shared memory and starts all needed threads */
void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right)
{
	buff_init(&buff);
	sema_init(&ok_to_finish, 0);

	unsigned int num_threads = num_vehicles_left + num_emergency_left + num_vehicles_right + num_emergency_right;

	struct data emergency_left;
	data_init(&emergency_left, 0, 1);

	while (num_emergency_left > 0) 
	{
	   	thread_create("left_emergency", 1, one_vehicle, &emergency_left);
	   	num_emergency_left--;
	}

	struct data emergency_right;
	data_init(&emergency_right, 1, 1);

	while (num_emergency_right > 0) 
	{
		thread_create("right_emergency", 1, one_vehicle, &emergency_right);
		num_emergency_right--;
	}

	struct data normal_left;
	data_init(&normal_left, 0, 0);

    while (num_vehicles_left > 0) 
    {		
		thread_create("left", 0, one_vehicle, &normal_left);	
		num_vehicles_left--;
    }		
		
	struct data normal_right;
	data_init(&normal_right, 1, 0);

    while (num_vehicles_right > 0) 
    {
		thread_create("right", 1, one_vehicle, &normal_right);
		num_vehicles_right--;	
    }

	/* If there were threads created, wait until last thread exits bridge */
	if (num_threads != 0)
		sema_down(&ok_to_finish);
}

/* Initializes data struct (direction, priority) */
void data_init(struct data* d, unsigned int dir, unsigned int pr) 
{
	d->direc = dir;
	d->prio = pr;
}

/* Initializes buffer struct */
void buff_init(struct buff* b)
{
	sema_init(&b->mutex, 1);
	sema_init(&b->bridge_left_normal, 0);
	sema_init(&b->bridge_right_normal, 0);
	sema_init(&b->bridge_left_emergency, 0);
	sema_init(&b->bridge_right_emergency, 0);

	b->active_left = 0;
	b->active_right = 0;
	b->waiting_left_normal = 0;
	b->waiting_right_normal = 0;
	b->waiting_left_emergency = 0;
	b->waiting_right_emergency = 0;
}


/* direc = 0, left to right; direc = 1, right to left; 
   prio = 0, no emergency; prio = 1, emergency */
void one_vehicle (void* data) 
{
	struct data* d = data;

	sema_down(&buff.mutex);
    unsigned int direc = d->direc;
	unsigned int prio = d->prio;		
	sema_up(&buff.mutex);
    
    arrive_bridge(direc, prio);
}

/* DEBUG: Prints shared memory values */
void print_stats(struct buff* buff)
{
	printf("-----------------------\n");

	printf("active_left: %d \n", buff->active_left);
	printf("active_right: %d \n", buff->active_right);
	printf("waiting_left_normal: %d \n", buff->waiting_left_normal);
	printf("waiting_right_normal: %d \n", buff->waiting_right_normal);
	printf("waiting_left_emergency: %d \n", buff->waiting_left_emergency);
	printf("waiting_right_emergency: %d \n", buff->waiting_right_emergency);
	
	printf("Sema left normal: %d \n", buff->bridge_left_normal.value);
	printf("Sema right normal: %d \n", buff->bridge_right_normal.value);

	printf("-----------------------\n");
}

/* Vehicle arrives at the bridge with direction and priority. 
   Using multiple semaphores and state variables, decides if vehicle can proceed
   over the bridge or has to wait.*/
void arrive_bridge(unsigned int direc, unsigned int prio)
{
	// Drive from left to right
	if (direc == 0)
	{
		// Normal vehicle
		if (prio == 0)
		{
			sema_down(&buff.mutex);

			// Only drive if there are no other emergency cars waiting and no opposite normal vehicles activ
			if (buff.waiting_left_emergency + buff.waiting_right_emergency + buff.active_right == 0 && buff.active_left < 3)
			{
				(buff.active_left)++;				
				sema_up(&buff.bridge_left_normal);
			}
			else 
			{
				(buff.waiting_left_normal)++;
			}
	
			sema_up(&buff.mutex);

			// Proceed if sema not 0, otherwise wait
			sema_down(&buff.bridge_left_normal);
		}
		// Emergency vehicle
		else
		{
			sema_down(&buff.mutex);

			// Only drive if there are no other emergency cars waiting and no opposite normal vehicles activ
			if (buff.waiting_left_emergency + buff.waiting_right_emergency + buff.active_right == 0 && buff.active_left < 3)
			{
				(buff.active_left)++;				
				sema_up(&buff.bridge_left_emergency);					
			} 
			else 
			{
				(buff.waiting_left_emergency)++;
			}

			sema_up(&buff.mutex);

			// Proceed if sema not 0, otherwise wait
			sema_down(&buff.bridge_left_emergency);
		}
	}
	// Drive from right to left
	else
	{
		// Normal vehicle
		if (prio == 0)
		{
			sema_down(&buff.mutex);

			// Only drive if there are no other emergency cars waiting and no opposite normal vehicles active
			if (buff.waiting_left_emergency + buff.waiting_right_emergency + buff.active_left == 0 && buff.active_right < 3)
			{
				(buff.active_right)++;				
				sema_up(&buff.bridge_right_normal);
			}
			else 
			{
				(buff.waiting_right_normal)++;
			}

			sema_up(&buff.mutex);

			// Proceed if sema not 0, otherwise wait
			sema_down(&buff.bridge_right_normal);
		}
		// Emergency vehicle
		else
		{
			sema_down(&buff.mutex);

			// Only drive if there are no other emergency cars waiting and no opposite normal vehicles active
			if (buff.waiting_left_emergency + buff.waiting_right_emergency + buff.active_left == 0 && buff.active_right < 3)
			{
				(buff.active_right)++;				
				sema_up(&buff.bridge_right_emergency);					
			} 
			else 
			{
				(buff.waiting_right_emergency)++;
			}

			sema_up(&buff.mutex);

			// Proceed if sema not 0, otherwise wait
			sema_down(&buff.bridge_right_emergency);
		}
	}
					
	// Start crossing the bridge
	cross_bridge(direc, prio);
}

/* Prints when entering, sleeps thread for random amount of ticks, prints whem exiting */
void cross_bridge(unsigned int direc, unsigned int prio)
{
	// Prints, when entering the bridge
	if (direc == 0) 
	{	
		if (prio == 0) 
		{	
			printf("%d: left: Entering the bridge... \n", (int) timer_ticks());
		} 	
		else 
		{
			printf("%d: left_emergency: Entering the bridge... \n", (int) timer_ticks());
		}
	} 
	else if (direc == 1)
	{
		if (prio == 0) 
		{
			printf("%d: right: Entering the bridge... \n", (int) timer_ticks());
		} 
		else 
		{
			printf("%d: right_emergency: Entering the bridge... \n", (int) timer_ticks());
		}
	} 

	// Get random number of ticks between 0 and 500
	random_init(timer_ticks());
	unsigned int r = random_ulong() % 500;

	// Put the thread to sleep for random ticks
  	timer_sleep(r);

	// Prints, when exiting the bridge
	if (direc == 0) 
	{	
		if (prio == 0) 
		{	
			printf("%d: left: Exit the bridge! \n", (int) timer_ticks());
		} 
		else 
		{
			printf("%d: left_emergency: Exit the bridge! \n", (int) timer_ticks());
		}
	} 
	else if (direc == 1)
	{
		if (prio == 0) 
		{
			printf("%d: right: Exit the bridge! \n", (int) timer_ticks());
		} 
		else 
		{
			printf("%d: right_emergency: Exit the bridge! \n", (int) timer_ticks());
		}
	} 

	// Start exiting the bridge
	exit_bridge(direc);
}

/* Decrements acitve vehicles and trys waking uo waiting emergency threads, if there are 
   no emergency vehicles, try waking the normal ones. If there's no left waiting car
   at all, up the ok_to_finish sema to finish the narrow bridge execution. */
void exit_bridge(unsigned int direc)
{
	sema_down(&buff.mutex);

	// Decrement corresponding active counter
	if (direc == 0)
		(buff.active_left)--;
	else
		(buff.active_right)--;

	// Check waiting emergency vehicles
	if (buff.waiting_left_emergency + buff.waiting_right_emergency != 0)
	{
		// If both are non zero		
		if (buff.waiting_left_emergency > 0 && buff.waiting_right_emergency > 0)
		{
			// Check which direction is currently active on the bridge
			if (buff.active_left > buff.active_right)
			{
				// Wake up all waiting emergency cars, if there are not more than 2 already active
				while (buff.waiting_left_emergency > 0 && buff.active_left < 3 && buff.active_right == 0)
				{
					sema_up(&buff.bridge_left_emergency);
					(buff.waiting_left_emergency)--;
					(buff.active_left)++;
				}
			}
			else
			{
				// Wake up all waiting emergency cars, if there are not more than 2 already active
				while (buff.waiting_right_emergency > 0 && buff.active_right < 3 && buff.active_left == 0)
				{
					sema_up(&buff.bridge_right_emergency);
					(buff.waiting_right_emergency)--;
					(buff.active_right)++;
				}
			}
		}
		// Only waiting right emergency cars
		else if (buff.waiting_right_emergency > 0)
		{
			// Wake up all waiting emergency cars, if there are not more than 2 already active
			while (buff.waiting_right_emergency > 0 && buff.active_right < 3 && buff.active_left == 0)
			{
				sema_up(&buff.bridge_right_emergency);
				(buff.waiting_right_emergency)--;
				(buff.active_right)++;
			}
		}
		// Only waiting left emergency cars
		else
		{
			// Wake up all waiting emergency cars, if there are not more than 2 already active
			while (buff.waiting_left_emergency > 0 && buff.active_left < 3 && buff.active_right == 0)
			{
				sema_up(&buff.bridge_left_emergency);
				(buff.waiting_left_emergency)--;
				(buff.active_left)++;
			}
		}
	}
	// Normal vehicles
	else
	{
		// Check waiting emergency vehicles
		if (buff.waiting_left_normal > 0 && buff.waiting_right_normal > 0) 
		{
			// Check which direction is currently active on the bridge
			if (buff.active_left > buff.active_right)
			{
				// Wake up all waiting emergency cars, if there are not more than 2 already active
				while (buff.waiting_left_normal > 0 && buff.active_left < 3 && buff.active_right == 0)
				{
					sema_up(&buff.bridge_left_normal);
					(buff.active_left)++;
					(buff.waiting_left_normal)--;
				}
			}
			else
			{
				// Wake up all waiting emergency cars, if there are not more than 2 already active
				while (buff.waiting_right_normal > 0 && buff.active_right < 3 && buff.active_left == 0) 
				{
					sema_up(&buff.bridge_right_normal);
					(buff.active_right)++;
					(buff.waiting_right_normal)--;
				}
			}
		}
		// Only waiting left emergency cars
		else if (buff.waiting_left_normal > 0)
		{
			// Wake up all waiting emergency cars, if there are not more than 2 already active
			while (buff.waiting_left_normal > 0 && buff.active_left < 3 && buff.active_right == 0)
			{
				sema_up(&buff.bridge_left_normal);
				(buff.active_left)++;
				(buff.waiting_left_normal)--;
			}		
		}
		// Only waiting right emergency cars
		else
		{
			// Wake up all waiting emergency cars, if there are not more than 2 already active
			while (buff.waiting_right_normal > 0 && buff.active_right < 3 && buff.active_left == 0) 
			{
				sema_up(&buff.bridge_right_normal);
				(buff.active_right)++;
				(buff.waiting_right_normal)--;
			}
		}
	}
			
	// Check if the current exiting vehicle is the very last one, then up teh ok_to_finish semaphore
	if(buff.active_left + buff.active_right + buff.waiting_left_normal + 
		buff.waiting_right_normal + buff.waiting_left_emergency + 
		buff.waiting_right_emergency == 0) 
	{
		sema_up(&ok_to_finish);
	}

	sema_up(&buff.mutex);
}


