/*
						CS-1550 Spring 2014 - Project 2 - The Producer/Consumer Problem
						 header file for Program #2: Semaphore with Peterson's Solution

										Written by Tyler Raborn
*/

#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <sys/time.h>

#define DEBUG_ENABLE 0 /* debug switch; set to 1 to activate readout */

#define NUM_THREADS 2
#define true 1
#define false 0

#ifndef _SEMAPHORE_
#define _SEMAPHORE_
typedef struct _Semaphore
{
	int tracker;
	int adjustment_count;

	int turn;
	int interested[2];

} Semaphore;
#endif

#ifndef _DATA_PACKET_
#define _DATA_PACKET_
typedef struct _Data
{
	int buffer_size;

} Data;
#endif

/* function prototypes */
void *producer(void*);
void semaphore_up(Semaphore*); /* internal semaphore adjustments are thread-protected via Peterson's Solution */
void semaphore_down(Semaphore*); /* internal semaphore adjustments are thread-protected via Peterson's Solution */
void thread_sleep(int);
void thread_wakeup(int);
static void cleanup();
static void error(const char*);