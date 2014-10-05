/* @Developer: Tyler Raborn
 * @Assignment: Project 1
 * @Class: CS-1550 Intro to Operating Systems, M/W 4:30-5:45pm
 * @Instructor: Dr. Misurda
 * @Due: Thursday, February 6, 2014 by 11:59pm
 */

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>

#define MAX_USER_NAME_LENGTH 50
#define MAX_MESSAGE_LENGTH 1000
#define TRUE 1
#define FALSE 0

static const char *ERROR_INVALID_ARGS = "ERROR: Invalid number of arguments.\n";
static const char *ERROR_MSG_SEND_FAILURE = "ERROR: The kernel reports that the sys_cs1550_send_msg() system call has failed.\n";
static const char *ERROR_MSG_GET_FAILURE = "ERROR: The kernel reports that the sys_cs1550_get_msg() system call has failed.\n";

static void error(const char*); /* error handling */

int main(int argc, char **argv)
{
	if (argc == 2) /* get message mode */
	{
		int is_final_message = FALSE;
		long get_message = 0;
		char *message_target = getenv("USER");
		char message[MAX_MESSAGE_LENGTH];
		char message_origin[MAX_USER_NAME_LENGTH];

		while (is_final_message == FALSE)
		{
			get_message = (long) syscall(            /* utilize sys_cs1550_get_msg() system call*/
				                         326, 
				                         message_target, 
				                         message, 
				                         (MAX_MESSAGE_LENGTH * sizeof(char)),
				                         message_origin,
				                         (MAX_USER_NAME_LENGTH * sizeof(char))
				                        ); 

			if (get_message >= 0)
			{
				printf("MESSAGE FROM USER %s: %s\n", message_origin, message); /* Display message to user */
			}

			if (get_message == 0)
			{
				is_final_message = TRUE; /* loop ending condition */
			}
			else if (get_message < 0)
			{
				printf("You have no messages.\n");
				return EXIT_SUCCESS;
				//error(ERROR_MSG_GET_FAILURE);
			}
		}
	}
	else if (argc > 3) /* send message mode */
	{
		long send_message = 0;
		char *message_origin = getenv("USER");
		char message[MAX_MESSAGE_LENGTH];

		send_message = (long) syscall(             /* utilize sys_cs1550_send_msg() system call */
							          325,
							          argv[2],
							          argv[3],
							          message_origin
			                         );

		if (send_message < 0)
		{
			error(ERROR_MSG_SEND_FAILURE);
		}
		printf("Message succesfully sent to user %s.\n", argv[2]);
	}
	else
	{
		error(ERROR_INVALID_ARGS);
	}

	return EXIT_SUCCESS;
}

static void error(const char *msg) /* error handling */
{
	perror(msg);
	exit(-1);
}

