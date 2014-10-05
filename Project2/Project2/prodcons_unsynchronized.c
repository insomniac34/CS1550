/*
						CS-1550 Spring 2014 - Project 2 - The Producer/Consumer Problem
							 Program #1: Unsynchronized Counter with Sleep/Wakeup

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

/* inter-process communication */
sigset_t set;
int PROD_SIGNAL = SIGUSR1;
int CONS_SIGNAL = SIGUSR2;
int CONSUMER_THREAD;
int PRODUCER_THREAD;

int n;

int in = 0;
int out = 0;
int counter = 0;

Item *buffer;

pthread_t thread_list[2];

void *producer(void);
void thread_sleep(int);
void thread_wakeup(int);

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

	if (argc == 3)
	{
		MAX_VAL = atoi(argv[2]);
	}

	n = atoi(argv[1]);

	buffer = (Item*)malloc(sizeof(Item)*n);

	/* inform system of new signal */
	sigemptyset(&set);
	sigaddset(&set, PROD_SIGNAL);
	sigaddset(&set, CONS_SIGNAL);

	int thread_mask = pthread_sigmask(SIG_BLOCK, &set, NULL); /* attempt to block signal */

	if (thread_mask != 0)
	{
		puts("Error modifying signal.");
		exit(-1);
	}	

	#if DEBUG_ENABLE
		printf("DEBUG: Succesfully modified signal values.\n");
	#endif 

	gettimeofday (&tv ,   &tz);
	time_start = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;

	int create_thread = pthread_create(
									   &thread_list[1],
									   NULL,
									   (void*)producer,
									   NULL
									  );

	Item citm;
	thread_list[0] = pthread_self();
	CONSUMER_THREAD = (int)pthread_self();
	while (1) 
	{
		if (counter == 0) thread_sleep(CONSUMER_THREAD);

		citm = buffer[out];
		out = (out+1) % n;
		counter -= 1; /* ATOMIC STATEMENT */

		if (counter == n-1) thread_wakeup(PRODUCER_THREAD);

		printf("Consumer Consumed: %d\n", (int)citm);

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
	printf("\nResults for Program #1 running on a %d-core CPU for %d iterations with a buffer size of %d: \nStart Time: %1fns\nEnd Time: %1fns\nElapsed Time: %1fns\nProgram 1 Total Throughput: %f\n\n", core_count, atoi(argv[2]), n, time_start, time_end, elapsed_time, throughput);

	free(buffer);
	return 0;
}

void *producer(void)
{
	PRODUCER_THREAD = (int)pthread_self();
	Item pitm = 0;
	while (1) 
	{
		printf("     Producer Produced: %d\n", (int)pitm++);

		if (counter == n) thread_sleep(PRODUCER_THREAD);

		buffer[in] = pitm;
		in = (in+1) % n;
		counter += 1; /* ATOMIC STATEMENT */

		if (counter==1) thread_wakeup(CONSUMER_THREAD);

		if (MAX_VAL != -1 && (int)pitm > MAX_VAL)
		{
			break;
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