/*
						CS-1550 Spring 2014 - Project 2 - The Producer/Consumer Problem
						     	  Program #3: Synchronized Counter with Mutexes

										Written by Tyler Raborn
*/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#define DEBUG_ENABLE 0 /* debug switch; set to 1 to activate readout */

typedef int Item;

int MAX_VAL = -1;

int n;
Item *buffer;
pthread_t thread_list[2];

/* mutex infrastructure */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t producer_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t consumer_condition = PTHREAD_COND_INITIALIZER;

int in = 0;
int out = 0;
int counter = 0;

void *producer(void);

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

	if (argc !=2 && argc != 3)
	{
		puts("Invalid args! Exiting!");
		exit(-1);
	}
	else if (argc == 3)
	{
		MAX_VAL = atoi(argv[2]);
	}

	n = atoi(argv[1]);

	buffer = (Item*)malloc(sizeof(Item)*n); /* allocate buffer */

	gettimeofday (&tv ,   &tz);
	time_start = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;

	/* create producer thread */
	int create_thread = pthread_create(
									   &thread_list[1],
									   NULL,
									   (void*)producer,
									   NULL
									  );

	Item citm;
	thread_list[0] = pthread_self();
	while (1) 
	{
		pthread_mutex_lock(&mutex); /* lock mutex */

		if (counter == 0)
		{
	    	#if DEBUG_ENABLE
	    		printf("DEBUG: CONSUMER condition is being set to WAIT\n"); /* DEBUG */
	    	#endif	

			pthread_cond_wait(						/*send wait signal to consumer */
						      &consumer_condition,
						      &mutex
							 );
		}

		citm = buffer[out]; /* get updated value */
		out = (out+1) % n;
		counter -= 1; /* ATOMIC STATEMENT */

		if (counter == n-1)
		{
	    	#if DEBUG_ENABLE
	    		printf("DEBUG: CONSUMER thread sending wakeup signal to PRODUCER thread.\n"); /* DEBUG */
	    	#endif		

			pthread_cond_signal(&producer_condition);
		}
		
		printf("Consumer Consumed: %d\n", (int)citm); /* consume */
		
		pthread_mutex_unlock(&mutex); /* unlock mutex */

		if (MAX_VAL != -1 && (int)citm > MAX_VAL)
		{
			break;
		}
	}		

	int join_thread = pthread_join(thread_list[1], NULL);

	gettimeofday (&tv, &tz);
	time_end = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;	

	float elapsed_time = (time_end - time_start);
	float throughput = 2 / elapsed_time;
	printf("\nResults for Program #3 running on a %d-core CPU for %d iterations with a buffer size of %d: \nStart Time: %1fns\nEnd Time: %1fns\nElapsed Time: %1fns\nProgram 3 Total Throughput: %f\n\n", core_count, atoi(argv[2]), n, time_start, time_end, elapsed_time, throughput);
	
	free(buffer);
	return 0;
}

void *producer(void)
{
	Item pitm = 0;

	while (1) 
	{
		printf("     Producer Produced: %d\n", (int)pitm++); 

		pthread_mutex_lock(&mutex); /* lock mutex */
		if (counter == n)
		{
	    	#if DEBUG_ENABLE
	    		printf("DEBUG: PRODUCER condition is being set to WAIT\n"); /* DEBUG */
	    	#endif			

			pthread_cond_wait(       				/*set producer thread to wait */
				              &producer_condition, 
				              &mutex
				             );
		}

		buffer[in] = pitm; /*enter pitm into buffer */
		in = (in+1) % n; /* update index */
		counter += 1; /* ATOMIC STATEMENT */

		if (counter == 1)
		{
	    	#if DEBUG_ENABLE
	    		printf("DEBUG: PRODUCER thread sending wakeup signal to CONSUMER thread.\n"); /* DEBUG */
	    	#endif			

			pthread_cond_signal(&consumer_condition); /* wakeup consumer */
		}
		pthread_mutex_unlock(&mutex);

		if (MAX_VAL != -1 && (int)pitm > MAX_VAL)
		{
			break;
		}
	}
	return NULL;
}
