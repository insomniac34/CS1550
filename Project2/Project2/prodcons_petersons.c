/*
						CS-1550 Spring 2014 - Project 2 - The Producer/Consumer Problem
								Program #2: Protected Semaphore with Peterson's Solution

										Written by Tyler Raborn
*/

#include "prodcons_petersons.h"

int MAX_VAL = -1; /* for timing */										

/* inter-process communication */
sigset_t set;
int PROD_SIGNAL = SIGUSR1;
int CONS_SIGNAL = SIGUSR2;

/* buffer components */
int *buffer; /* to be dynamically allocated to the user-defined size */
int buf_size;
int item = 0;
int citem = 0;
int in = 0;
int out = 0;

/* thread infrastructure */
pthread_t thread_list[NUM_THREADS]; /* thread data structure */
int CONSUMER_THREAD;
int PRODUCER_THREAD;
int consumer_handle = 0;
int producer_handle = 1;

/* behold, C's zany constructors */
Semaphore empty = {				
			       .tracker = 0,
			       .adjustment_count = 0,
			       .turn = 2,
			       .interested = {false, false}
			      }; 

Semaphore full = {
				  .tracker = 0,
				  .adjustment_count = 0,
				  .turn = 2,
				  .interested = {false, false}
				 };

Semaphore mutex = {
				   .tracker = 1,
				   .adjustment_count = 0,	
				   .turn = 2,
				   .interested = {false, false}
				  };

/* error messages */
const static char *ERROR_INVALID_ARGS = "ERROR: Invalid number of arguments. Usage: ./program <number> <debug_switch>\n";
const static char *ERROR_SIG_MASK = "ERROR: the call to pthread_sigmask has failed.\n";
const static char *ERROR_CREATE_PRODUCER = "ERROR: the call to pthread_create() for the PRODUCER thread has failed.\n";
const static char *ERROR_CREATE_CONSUMER = "ERROR: the call to pthread_create() for the CONSUMER thread has failed.\n";
const static char *ERROR_THREAD_JOIN = "ERROR: The call to pthread_join() has failed.\n";

int main(int argc, char **argv)
{
	/* time keeping infrastructure */
	double time_start, time_end;
	struct timeval tv;
	struct timezone tz;

	/* get cpu information from system */
	int core_count;
	char cpu_name[32];
	FILE *fd0 = popen("cat /proc/cpuinfo | egrep \"core id|physical id\" | tr -d \"\n\" | sed s/physical/\\\\nphysical/g | grep -v ^$ | sort | uniq | wc -l", "r");
	fscanf(fd0, "%d", &core_count);
	pclose(fd0);			

	if (argc != 3 && argc != 2)
	{
		error(ERROR_INVALID_ARGS);
	}
	else
	{
		if (argc == 3)
		{
			MAX_VAL = atoi(argv[2]);
		}

		#if DEBUG_ENABLE
			printf("DEBUG: Proceeding into program body.\n");  /* DEBUG */
		#endif

	     cpu_set_t cpuset;
	     CPU_ZERO(&cpuset);
	     CPU_SET(0, &cpuset);
	 
	     if(sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset) != 0)
	     {
	           perror("Setting affinity failed\n");
	           exit(-1);
	     }		

		int arg = atoi(argv[1]);
		buf_size = arg;
		empty.tracker = arg;

		buffer = (int*)malloc(sizeof(int)*arg); /* allocate buffer on the heap */

		#if DEBUG_ENABLE
			printf("DEBUG: Succesfully allocated buffer.\n");  /* DEBUG */
		#endif

		/* inform system of new signal */
		sigemptyset(&set);
		sigaddset(&set, PROD_SIGNAL);
		sigaddset(&set, CONS_SIGNAL);

		int thread_mask = pthread_sigmask(SIG_BLOCK, &set, NULL); /* attempt to block signal */

		#if DEBUG_ENABLE
			printf("DEBUG: Succesfully modified signal values.\n");
		#endif 

		if (thread_mask != 0)
		{
			error(ERROR_SIG_MASK);
		}

		int create_producer;
		Data producer_data;
		producer_data.buffer_size = arg;

		gettimeofday (&tv ,   &tz);
		time_start = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;		

		create_producer = pthread_create(                      /* create producer thread */
										 &thread_list[1],
										 NULL,
										 (void*)producer,
										 (void*)&producer_data
									    );

		if (create_producer != 0)
		{
			error(ERROR_CREATE_PRODUCER);
		}

		#if DEBUG_ENABLE
			printf("DEBUG: Producer thread created!\n");  /* DEBUG */
		#endif

		//CONSUMER
		thread_list[0] = pthread_self();
		CONSUMER_THREAD = pthread_self();
		while(true)
		{	
			semaphore_down(&full);
			semaphore_down(&mutex);

			#if DEBUG_ENABLE
				printf("DEBUG: Consumer is inside of critical section.\n"); /* DEBUG */
			#endif

			citem = buffer[out];
			out = (out + 1) % arg;

			semaphore_up(&mutex);
			semaphore_up(&empty);

			printf("Consumer Consumed: %d\n", citem);

			if (MAX_VAL != -1 && (int)citem > MAX_VAL)
			{
				break;
			}			
		}

		int join_thread = pthread_join(
			                           thread_list[1], 
			                           NULL
			                          );

		if (join_thread != 0)
		{
			error(ERROR_THREAD_JOIN);
		}	

		gettimeofday (&tv, &tz);
		time_end = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;	

		float elapsed_time = (time_end - time_start);
		float throughput = 2 / elapsed_time;
		printf("\nResults for Program #2 running on a %d-core CPU for %d iterations with a buffer size of %d: \nStart Time: %1fns\nEnd Time: %1fns\nElapsed Time: %1fns\nProgram 2 Total Throughput: %f\n\n", core_count, atoi(argv[2]), buf_size, time_start, time_end, elapsed_time, throughput);
	}	

	atexit(cleanup); /* free heap memory */
	return EXIT_SUCCESS;
}

void *producer(void *args)
{
	Data *producer_data = (Data*)args;
	int buffer_size = producer_data->buffer_size;

	PRODUCER_THREAD = (int)pthread_self();

	while(true)
	{	
		printf("    Producer Produced: %d\n", item++); /* produce an item */
		
		semaphore_down(&empty);
		semaphore_down(&mutex);

		buffer[in] = item;
		in = (in+1) % buffer_size; 

		#if DEBUG_ENABLE
			printf("DEBUG: Producer is inside of critical section.\n"); /* DEBUG */
		#endif

		semaphore_up(&mutex);
		semaphore_up(&full);

		if (MAX_VAL != -1 && (int)item > MAX_VAL)
		{
			break;
		}		
	} 

	return NULL;
}

void semaphore_up(Semaphore *target) /* this method utilizes Peterson's Solution to implement protected semaphore ops */
{

	int CURRENT_THREAD, OTHER_THREAD;
	if ((int)pthread_self() == CONSUMER_THREAD)
	{
		CURRENT_THREAD = consumer_handle;
		OTHER_THREAD = producer_handle;
	}
	else
	{
		CURRENT_THREAD = producer_handle;
		OTHER_THREAD = consumer_handle;
	}

	#if DEBUG_ENABLE
		if (target == &empty) printf("DEBUG: Empty is inside of semaphore_up()\n"); 
		else if (target == &full) printf("DEBUG: Full is inside of semaphore_up()\n");
		else if (target == &mutex) printf("DEBUG: Mutex is inside of semaphore_up()\n"); 
	#endif

	target->interested[CURRENT_THREAD] = true;
	target->turn = OTHER_THREAD;

	while (target->turn == OTHER_THREAD && target->interested[OTHER_THREAD] == true)
	{
		//busy wait...

		#if DEBUG_ENABLE
			if (target == &empty)printf("empty is busy waiting in semaphore up\n"); 
			else if (target == &full)printf("full is busy waiting in semaphore up\n"); 
			else printf("mutex is busy waiting in semaphore up\n"); 			
		#endif
	}

	target->tracker++; /* CRITICAL SECTION */
	//if (target == &mutex) printf("****************Current value of mutex is %d\n", target->tracker);

	target->interested[CURRENT_THREAD] = false;

	//if counter == 1 wakeup consumer 
	//if (target->tracker <= 0 && target != &mutex) 
	if (target->tracker <= 0)
	{
		if (CURRENT_THREAD == producer_handle)
		{
			thread_wakeup(CONSUMER_THREAD);
		}
		else
		{
			thread_wakeup(PRODUCER_THREAD);	
		}
	}	
}

void semaphore_down(Semaphore *target) /* this method utilizes Peterson's Solution to implement protected semaphore ops */
{

	int CURRENT_THREAD, OTHER_THREAD;
	if ((int)pthread_self() == CONSUMER_THREAD)
	{
		CURRENT_THREAD = consumer_handle;
		OTHER_THREAD = producer_handle;
	}
	else
	{
		CURRENT_THREAD = producer_handle;
		OTHER_THREAD = consumer_handle;
	}

	#if DEBUG_ENABLE
		if (target == &empty)printf("DEBUG: Empty calling semaphore_up()\n"); 
		else if (target == &full)printf("DEBUG: Full is calling semaphore_up()\n"); 
		else if (target == &mutex)printf("DEBUG: Mutex is calling semaphore_up()\n"); 
	#endif

	target->interested[CURRENT_THREAD] = true;
	target->turn = OTHER_THREAD;

	while (target->turn == OTHER_THREAD && target->interested[OTHER_THREAD] == true)
	{
		//busy wait...

		#if DEBUG_ENABLE
			if (target == &empty)printf("empty is busy waiting in semaphore down\n"); 
			else if (target == &full)printf("full is busy waiting in semaphore down\n"); 
			else printf("mutex is busy waiting in semaphore down\n"); 
		#endif
	}

	target->tracker--; /* CRITICAL SECTION */

	target->interested[CURRENT_THREAD] = false;

	//if (target->tracker < 0 && target != &mutex)
	if (target->tracker < 0)
	{
		if (CURRENT_THREAD == producer_handle)
		{
			thread_sleep(PRODUCER_THREAD);
		}
		else
		{	
			thread_sleep(CONSUMER_THREAD);	
		}
	}
}

/* sleep the thread identified by the parameter */
void thread_sleep(int tid) 
{	
    int sig;
    if (tid == CONSUMER_THREAD)
    {
    	#if DEBUG_ENABLE
    		printf("DEBUG: CONSUMER is going to sleep\n"); /* DEBUG */
    	#endif

	    sigemptyset(&set);
	    sigaddset(&set, CONS_SIGNAL);
	    sigwait(&set, &sig);    	
    }
    else
    {
    	#if DEBUG_ENABLE
    		printf("DEBUG: PRODUCER is going to sleep\n"); /* DEBUG */
    	#endif

	    sigemptyset(&set);
	    sigaddset(&set, PROD_SIGNAL);
	    sigwait(&set, &sig);       	
    }
}

/* wakeup the thread identified by the parameter */
void thread_wakeup(int tid)
{
	if (tid == CONSUMER_THREAD)
	{
		#if DEBUG_ENABLE
			printf("DEBUG: CONSUMER is getting woken up\n"); /* DEBUG */
		#endif

		pthread_kill(thread_list[0], CONS_SIGNAL);	
	}
	else
	{
		#if DEBUG_ENABLE
			printf("DEBUG: PRODUCER is getting woken up\n"); /* DEBUG */
		#endif

		pthread_kill(thread_list[1], PROD_SIGNAL);
	}
}

static void cleanup()
{
	free(buffer);
}

static void error(const char *msg)
{
	perror(msg);
	exit(-1);
}