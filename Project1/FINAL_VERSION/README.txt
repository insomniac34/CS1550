Developer: Tyler Raborn
Class: CS1550
Assignment: Project 1
Due: Feb. 6th @ Midnight

FILES:
unistd.h
syscall_table.S
sys.c
osmsg.c

USAGE:
./osmsg -r ; for retrieval
./osmsg -s <target> <msg> ; for sending

BUGS:
No bugs appeared during my testing.

DESCRIPTION:
I used a singly-linked list for simplicity of implementation; a global pointer to the list's head node
ensures data is kept track of at all times, and the list methods perform the proper operations to ensure
integrity of the list.
