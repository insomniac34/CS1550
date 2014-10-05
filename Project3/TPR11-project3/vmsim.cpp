/*
												CS1550 Project 3 - Page Table & Virtual Memory Algorithm Simulator
																	Written by Tyler Raborn


	COMPILE COMMAND: g++ -O2 -std=gnu++0x -o vmsim vmsim.cpp
	RUN COMMAND: ./vmsim –n <numframes> -a <opt|clock|nru|rand> [-r <NRUrefresh>] <tracefile>
*/

#define DEBUG_ENABLE 0

//C++ Headers	
#include <fstream>
#include <string>
#include <deque>
#include <unordered_map>
#include <map>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <vector>

//C Headers
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define PAGE_SIZE 4096 // bytes
#define PAGE_TABLE_SIZE 1048576

#define OPT 0
#define CLOCK 1
#define NRU 2
#define RANDOM 3

bool VERBOSE_MODE = false;

#ifndef _PAGE_
#define _PAGE_
class Page
{

public:

	Page();
	Page(char, unsigned int);

	void set_valid_bit(int);
	int get_valid_bit();

	void set_reference_bit(int);
	int get_reference_bit();

	void set_dirty_bit(int);
	int get_dirty_bit();
	
	void set_status(bool);

	unsigned int get_address();
	int get_usage_status();

	~Page();

private:

	unsigned int page_address;

	int V;
	int R;
	int D;

	std::string location;
};
#endif

Page::Page(char state, unsigned int address)
{

	this->R = 0;
	this->V = 0;
	this->D = 0;
	this->page_address = address;
}

void Page::set_valid_bit(int v)
{
	this->V = v;
}

int Page::get_valid_bit()
{
	return this->V;
}

void Page::set_reference_bit(int r)
{
	this->R = r;
}

int Page::get_reference_bit()
{
	//printf("in get ref bit\n");
	return this->R;
}

void Page::set_dirty_bit(int d)
{
	this->D = d;
}

int Page::get_dirty_bit()
{
	return this->D;
}

unsigned int Page::get_address()
{
	return this->page_address;
}

int Page::get_usage_status()
{
	if ((this->R == 1) && (this->D == 1))
	{
		return 3;
	}
	else if ((this->R == 1) && (this->D == 0))
	{
		return 2;
	}
	else if ((this->R == 0) && (this->D == 1))
	{
		return 1;
	}
	else //this page is not referenced nor is it modified.
	{
		return 0;
	}
}

Page::~Page()
{

}

int hit_count = 0;
int fault_count = 0;
int disk_write_count = 0;
int access_count = 0;
int compulsory_miss_count = 0;

std::deque<Page*> addr_table; //the page table itself, a deque of pointers to Page objects
std::deque<Page*>::iterator clock_iterator; //iterator for usage in the clock algorithm.
std::unordered_map<unsigned int, Page*> disk_table; //this is the main pool of pages. Throughout the entire program, all pages actually exist here, and all pages evicted and inserted are actually pointers to these pages.

//4 deques that hold the 4 types of NRU page classifications
std::deque<std::pair<int, Page*> > nru_type_0_list;
std::deque<std::pair<int, Page*> > nru_type_1_list;
std::deque<std::pair<int, Page*> > nru_type_2_list;
std::deque<std::pair<int, Page*> > nru_type_3_list;

std::map<unsigned int, std::deque<int>*> preproc_table; //maps the references with a pointer to a deque of their references in the file.
std::map<unsigned int, bool> sorted; //determines whether the deque holding occurences of a particular instruction has been sorted or not. Used to speed up the opt algorithm loop.
int sort_count = 0;

bool initial_run = true;
int frame_count;
int algorithm_id;
int refresh_interval = -1;
int refresh_counter = 0;

int nru_type_3_count = 0;
int nru_type_2_count = 0;
int nru_type_1_count = 0;
int nru_type_0_count = 0;

static void report(double);
static void cleanup();
bool page_lookup_predicate(Page*, Page*);
std::deque<Page*>::iterator *page_lookup(std::deque<Page*>*, Page*);
void update_nru_type_counts();
bool optimal_distance_predicate(int, int);
int get_optimal_distance(std::deque<int>*, int);


int main(int argc, char **argv)
{
	srand(time(NULL)); //seed for pseudorandom number generator

	std::deque<std::string> arg_list;
	for (int i = 0; i < argc; i++)
	{
		arg_list.push_front(std::string(argv[i]));
	}

	if (arg_list.end() != std::find(arg_list.begin(), arg_list.end(), "-v"))
	{
		char v_ans;
		printf("\nVMSIM: WARNING: Running the simulation under verbose mode can drastically increase program runtime. Continue? (y/n)\n");
		scanf("%c", &v_ans);

		if (v_ans == 'y')
		{
			VERBOSE_MODE = true;
		}
		argc--;
	} 

	double time_start, time_end;
	struct timeval tv;
	struct timezone tz;
	gettimeofday (&tv, &tz);
	time_start = (double)tv.tv_sec + (double)tv.tv_sec;	

	if (argc != 6 && argc != 8)
	{
		printf("\nVMSIM: invalid number of arguments (%d). Usage is ./program -n <# of frames> -a <algorithm> <trace file> OR ./program -n <# of frames> -a <algorithm> -r <refresh interval> <trace file>\n", argc);
		return -1;
	}
	else //(argc == 6 || argc == 8)
	{
		std::ifstream input_stream;
		if (strcmp(argv[4], "opt") == 0)
		{
			/*
				the OPT algorithm works as follows:

					the idea is to use a hash table that maps addresses to their LAST OCCURENCE in the file. That is, the address is the key into the hashmap and the value stored
					is that address's distance from the BEGINNING of the file. This will allow the optimal algorithm to iterate over the addresses in memory and find the one with the LOWEST 
					mapped distance value, thereby finding the optimal eviction target.
			*/

			algorithm_id = OPT;
			
			std::ifstream preproc_stream;
			preproc_stream.open(argv[5]);
			std::string preproc_line;

			unsigned int cur_addr;
			int distance_to_bof = 0;

			while (std::getline(preproc_stream, preproc_line))
			{
				cur_addr = (unsigned int)((unsigned int)strtol(preproc_line.assign(preproc_line.c_str(), 8).c_str(), NULL, 16) / PAGE_SIZE);
				distance_to_bof++;

				if (preproc_table.find(cur_addr) == preproc_table.end()) //if we are inserting a unique value, make a new pair and insert it
				{
					std::deque<int> *dq = new std::deque<int>();
					dq->push_front(distance_to_bof);
					preproc_table.insert(std::pair<unsigned int, std::deque<int>*>(cur_addr, dq));
					sorted.insert(std::pair<unsigned int, bool>(cur_addr, false));
				}
				else //else if the address has already been mapped to, insert the current distance into its qeueue.
				{
					preproc_table[cur_addr]->push_front(distance_to_bof);
				}
			}

			printf("VMSIM: Optimal preprocessing complete. Initiating main algorithm.\n");

			preproc_stream.close(); //close preprocessing stream

			input_stream.open(argv[5]); //open new stream
		} 
		else if (strcmp(argv[4], "clock") == 0)
		{
			algorithm_id = CLOCK;
			input_stream.open(argv[5]);
		}
		else if (strcmp(argv[4], "nru") == 0)
		{
			if (argc == 8)
			{
				refresh_interval = atoi(argv[6]);
				input_stream.open(argv[7]);
			}
			else
			{
				refresh_interval = 10;
				input_stream.open(argv[5]);
				printf("\nVMSIM: Running NRU algorithm with default refresh interval of 10! To specify an alternate value at the command line, utilize the following arguments: ./program -n <# of frames> -a <algorithm> -r <refresh interval> <trace file> \n");
			}

			algorithm_id = NRU;
		}
		else if (strcmp(argv[4], "rand") == 0)
		{
			algorithm_id = RANDOM;
			input_stream.open(argv[5]);
		}
		else
		{
			printf("VMSIM: Unrecognized algorithm name. Options are rand, clock, opt or nru.\n");
			return EXIT_FAILURE;
		}

		frame_count = atoi(argv[2]);
		std::string line;
		
		char page_status;
		unsigned int addr;
		unsigned int page_number;

		while (std::getline(input_stream, line)) //read-in loop
		{
			access_count++;
			page_status = line.at(9);
			addr = (unsigned int)strtol(line.assign(line.c_str(), 8).c_str(), NULL, 16);
			page_number = addr / PAGE_SIZE;

			Page *current_page = new Page(page_status, page_number);
			disk_table.insert(std::pair<unsigned int, Page*>(page_number, current_page)); 

			std::deque<Page*>::iterator *addr_loc_ptr = (page_lookup(&addr_table, current_page)); //does the current page exist in memory?
			std::deque<Page*>::iterator addr_location = *addr_loc_ptr;
			if (addr_location == addr_table.end()) //if not, we have encountered a page fault. 
			{
				fault_count++;

				if ((int)addr_table.size() == frame_count || compulsory_miss_count <= frame_count) //if memory is full OR we are in the initial compulsory miss stage, we need to fetch a frame from disk.
				{
					compulsory_miss_count++;

					//evict a frame:
					if (algorithm_id == OPT) 
					{
						if (compulsory_miss_count > frame_count)
						{
							int optimal_distance = 0;
							int cur_optimal_distance = 0;
							std::deque<Page*>::iterator target_iterator = addr_table.begin();
							std::deque<int> *cur_list;
							target_iterator++;
							
							for (std::deque<Page*>::iterator eviction_iterator = addr_table.begin(); eviction_iterator != addr_table.end(); eviction_iterator++) //iterate over pages to find optimal eviction target.
							{
								cur_list = preproc_table[(*eviction_iterator)->get_address()];
								if (cur_list->size() == 1)
								{
									cur_optimal_distance =(*cur_list)[0];
								}
								else
								{
									if (sorted[(*eviction_iterator)->get_address()] == false) 
									{
										#if DEBUG_ENABLE
											if (VERBOSE_MODE)
												printf("Sorting deque of size %d. Total deques sorted: %d. Total lines processed: %d.\n", (int)preproc_table[(*eviction_iterator)->get_address()]->size(), sort_count, access_count);
										#endif

										std::sort(cur_list->begin(), cur_list->end());
										sorted[(*eviction_iterator)->get_address()] = true;
										sort_count++;
									}

									cur_optimal_distance = get_optimal_distance(cur_list, access_count);
								}

								if (cur_optimal_distance == -1)
								{
									target_iterator = eviction_iterator;
									break;
								}

								if (cur_optimal_distance > optimal_distance)
								{
									optimal_distance = cur_optimal_distance;
									target_iterator = eviction_iterator;
								}	
							}

							if ((*target_iterator)->get_dirty_bit() == 1) //if frame is DIRTY, we must take this opportunity to write back to disk
							{
								#if DEBUG_ENABLE
									if (VERBOSE_MODE)
										printf("OPTIMALLY selected page is DIRTY!\n");
								#endif

								disk_write_count++; //this is the simulation equivalent of "writing data to disk"
								(*target_iterator)->set_dirty_bit(0); //we can now reset the dirty bit, as the previous write has been logged.
								puts("page fault - evict dirty");
							}
							else
							{
								puts("page fault - evict clean");	
							}
							

							target_iterator = addr_table.erase(target_iterator); //we now literally remove the page from memory.
							Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
							addr_table.insert(target_iterator, target_page);

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}																	
						}
						else
						{
							Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
							//printf("target page has a value of %u\n", target_page->get_address());
							addr_table.push_front(target_page);

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}			
						}
					}
					else if (algorithm_id == CLOCK) 
					{
						//printf("clock_iterator is at location %d\n", (int)std::distance(addr_table.begin(), clock_iterator));
						if (compulsory_miss_count > frame_count)
						{
							if (initial_run == true)
							{
								clock_iterator = addr_table.begin();
								initial_run = false;
							}

							if ((*clock_iterator)->get_reference_bit() == 0) //the hand pointer has landed upon a page with its R-bit set to 0. As such, it has NOT been referenced recently. By definition of the clock algorithm, it is evicted.
							{
								if ((*clock_iterator)->get_dirty_bit() == 1) //if frame is DIRTY, we must take this oppurtunity to write back to disk
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("the page selected by the clock hand is DIRTY!\n");
									#endif

									disk_write_count++; //this is the simulation equivalent of "writing data to disk"
									(*clock_iterator)->set_dirty_bit(0); //we can now reset the dirty bit, as the previous write has been logged.
									puts("page fault - evict dirty");
								}
								else
								{
									puts("page fault - evict clean");
								}

								clock_iterator = addr_table.erase(clock_iterator);
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								target_page->set_reference_bit(1);

								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}			

								clock_iterator = addr_table.insert(clock_iterator, target_page);
								clock_iterator++;
						
								if (clock_iterator == addr_table.end()) //enforce circular structure
								{
									clock_iterator = addr_table.begin();
								}
							}
							else //else iterate over pages until target found
							{
								while ((*clock_iterator)->get_reference_bit() == 1) //increment hand pointer UNTIL IT HAS FOUND A SUITABLE INSERTION TARGET
								{
									(*clock_iterator)->set_reference_bit(0); //clear ref bit
									clock_iterator++;

									if (clock_iterator == addr_table.end()) //enforce circular structure
									{
										clock_iterator = addr_table.begin();
									}									
								}

								if ((*clock_iterator)->get_dirty_bit() == 1) //if frame is DIRTY, we must take this oppurtunity to write back to disk
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("the page selected by the clock hand is DIRTY!\n");
									#endif

									disk_write_count++; //this is the simulation equivalent of "writing data to disk"
									(*clock_iterator)->set_dirty_bit(0); //we can now reset the dirty bit, as the previous write has been logged.
									puts("page fault - evict dirty");
								}
								else
								{
									puts("page fault - evict clean");
								}

								clock_iterator = addr_table.erase(clock_iterator);
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								target_page->set_reference_bit(1);

								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}			

								clock_iterator = addr_table.insert(clock_iterator, target_page);
								clock_iterator++;
						
								if (clock_iterator == addr_table.end()) //enforce circular structure
								{
									clock_iterator = addr_table.begin();
								}				
							}
						}
						else //cumpolsory miss; add address to queue
						{
							Page *target_page = ((disk_table.find(page_number))->second); 
							target_page->set_reference_bit(1); //set ref bit
							target_page->set_dirty_bit(0);	

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}									

							addr_table.push_front(target_page);		
						}							
					}
					else if (algorithm_id == NRU) 
					{
						if (refresh_counter == refresh_interval) //reset all R-bits to 0
						{
							for (std::deque<Page*>::iterator page_iterator = addr_table.begin(); page_iterator != addr_table.end(); page_iterator++)
							{
								if ((*page_iterator)->get_reference_bit() == 1)
								{
									(*page_iterator)->set_reference_bit(0);
								}
							}

							refresh_counter = 0; //reset counter
						}
						else
						{
							refresh_counter++;								
						}

						if (compulsory_miss_count > frame_count)
						{
							/*
								The NRU algorithm counts the following 4 types of pages: 

									3. referenced, modified
									2. referenced, not modified
									1. not referenced, modified
									0. not referenced, not modified
							*/
							
							update_nru_type_counts(); //get updated values for page status variables

							if ((int)nru_type_0_list.size() > 0)
							{
								int target_frame = rand() % (int)nru_type_0_list.size();
								std::deque<std::pair<int, Page*> >::iterator target_iterator = nru_type_0_list.begin();
								std::advance(target_iterator, target_frame); //iterator now points to the std::pair object which contains: 1.) the index into the addr_table where the page is located, and the page itself.

								if (target_iterator->second->get_dirty_bit() == 1)
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("randomly selected page is DIRTY!\n");
									#endif

									disk_write_count++;
									target_iterator->second->set_dirty_bit(0);
									puts("page fault - evict dirty");
								}
								else
								{
									puts("page fault - evict clean");
								}

								addr_table.at(target_iterator->first) = NULL;
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								addr_table.at(target_iterator->first) = target_page;

								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}								
							}
							else if ((int)nru_type_1_list.size() > 0)
							{
								int target_frame = rand() % (int)nru_type_1_list.size();
								std::deque<std::pair<int, Page*> >::iterator target_iterator = nru_type_1_list.begin();
								std::advance(target_iterator, target_frame); //iterator now points to the std::pair object which contains: 1.) the index into the addr_table where the page is located, and the page itself.

								if (target_iterator->second->get_dirty_bit() == 1)
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("randomly selected page is DIRTY!\n");
									#endif

									disk_write_count++;
									target_iterator->second->set_dirty_bit(0);
									puts("page fault - evict dirty");
								}
								else 
								{
									puts("page fault - evict clean");
								}

								addr_table.at(target_iterator->first) = NULL;
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								addr_table.at(target_iterator->first) = target_page;

								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}								
							}
							else if ((int)nru_type_2_list.size() > 0)
							{
								int target_frame = rand() % (int)nru_type_2_list.size();
								std::deque<std::pair<int, Page*> >::iterator target_iterator = nru_type_2_list.begin();
								std::advance(target_iterator, target_frame); //iterator now points to the std::pair<int, Page*> templated object which contains: 1.) the index into the addr_table where the page is located, and the page itself.

								if (target_iterator->second->get_dirty_bit() == 1)
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("randomly selected page is DIRTY!\n");
									#endif

									disk_write_count++;
									target_iterator->second->set_dirty_bit(0);
									puts("page fault - evict dirty");
								}
								else 
								{
									puts("page fault - evict clean");
								}

								addr_table.at(target_iterator->first) = NULL;
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								addr_table.at(target_iterator->first) = target_page;
								
								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}
							}
							else
							{
								int target_frame = rand() % (int)nru_type_3_list.size();
								std::deque<std::pair<int, Page*> >::iterator target_iterator = nru_type_3_list.begin();
								std::advance(target_iterator, target_frame); //iterator now points to the std::pair object which contains: 1.) the index into the addr_table where the page is located, and the page itself.

								if (target_iterator->second->get_dirty_bit() == 1)
								{
									#if DEBUG_ENABLE
										if (VERBOSE_MODE)
											printf("randomly selected page is DIRTY!\n");
									#endif

									disk_write_count++;
									target_iterator->second->set_dirty_bit(0);
									puts("page fault - evict dirty");
								}
								else
								{
									puts("page fault - evict clean");
								}

								addr_table.at(target_iterator->first) = NULL;
								Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
								addr_table.at(target_iterator->first) = target_page;

								if (page_status == 'W')
								{
									target_page->set_dirty_bit(1);
								}
							}
						}
						else
						{
							Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
							addr_table.push_front(target_page);							

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}							
						}
					}
					else if (algorithm_id == RANDOM) 
					{
						if (compulsory_miss_count > frame_count) //assuming we are NOT in the initial compulsory miss timeframe, find frame to evict.
						{
							int target_frame = rand() % (int)addr_table.size();
							std::deque<Page*>::iterator target_iterator = addr_table.begin();
							std::advance(target_iterator, target_frame); //move to randomly selected frame

							#if DEBUG_ENABLE
								if (VERBOSE_MODE)
									printf("RANDOM algorithm is evicting frame # %d!\n", target_frame);
							#endif

							if ((*target_iterator)->get_dirty_bit() == 1) //if frame is DIRTY, we must take this oppurtunity to write back to disk
							{
								#if DEBUG_ENABLE
									if (VERBOSE_MODE)
										printf("randomly selected page is DIRTY!\n");
								#endif

								disk_write_count++; //this is the simulation equivalent of "writing data to disk"
								(*target_iterator)->set_dirty_bit(0); //we can now reset the dirty bit, as the previous write has been logged.
								puts("page fault - evict dirty");
							}
							else
							{
								puts("page fault - evict clean");
							}

							target_iterator = addr_table.erase(target_iterator); //we now literally remove the page from memory.
							Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
							addr_table.insert(target_iterator, target_page);

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}								
						}
						else
						{
							Page *target_page = ((disk_table.find(page_number))->second); //the page has already been added to disk in the read-in loop, so even if it is the FIRST memory access of its kind, it will have already been added to disk
							addr_table.push_front(target_page);

							if (page_status == 'W')
							{
								target_page->set_dirty_bit(1);
							}								
						}
					}
					else
					{
						printf("WHAT EVEN IS GOING ON\n");
						return EXIT_FAILURE;
					}
				}
				else
				{

				}	
			}
			else
			{
				#if DEBUG_ENABLE
					if (VERBOSE_MODE)
						printf("HIT! The page has been found in memory.\n");
				#endif
				
				(*addr_location)->set_reference_bit(1); //page has been referenced
				hit_count++;
				puts("hit");
				
				if (page_status == 'W')
				{
					#if DEBUG_ENABLE
						if (VERBOSE_MODE)
							printf("Writing to memory. Flipping dirty bit.\n");
					#endif

					(*addr_location)->set_dirty_bit(1);
				}
			}
			delete addr_loc_ptr;
		} //end main loop body

		input_stream.close();		

		#if DEBUG_ENABLE
			if (VERBOSE_MODE)
			{
				for (std::deque<Page*>::iterator iter = addr_table.begin(); iter != addr_table.end(); iter++)
				{
					printf("Page table contains: %u\n", (*iter)->get_address());
				}	
			}
		#endif
	} 

	gettimeofday (&tv, &tz);
	time_end = (double)tv.tv_sec + (double)tv.tv_sec;	

	report(time_end-time_start);
	cleanup();

	return EXIT_SUCCESS;
}

static void report(double elapsed_time)
{
	printf("\n\n******************* RESULTS for %d ************************\n", algorithm_id);
	printf("Elapsed Time: %d seconds\n", (int)elapsed_time);
	if (algorithm_id == OPT) printf("Algorithm: Optimal\n");
	else if (algorithm_id == CLOCK) printf("Algorithm: Clock\n");
	else if (algorithm_id == NRU) printf("Algorithm: NRU\n");
	else if (algorithm_id == RANDOM) printf("Algorithm: Randomized\n");
	printf("Number of Frames: %d\n", frame_count);
	if (algorithm_id == NRU) printf("NRU Refresh Interval: %d\n", refresh_interval); 
	printf("Total Memory Accesses: %d\n", access_count);
	//printf("Total Memory Hits: %d\n", hit_count);
	printf("Total Page Faults: %d\n", fault_count);
	printf("Total Writes to Disk: %d\n", disk_write_count);
	printf("****************************************************\n\n");
}

static void cleanup() //delete dynamic allocations
{
	for (std::unordered_map<unsigned int, Page*>::iterator iter = disk_table.begin(); iter != disk_table.end(); iter++)
	{
		delete iter->second;
	}	

	for (std::map<unsigned int, std::deque<int>*>::iterator iter = preproc_table.begin(); iter != preproc_table.end(); iter++)
	{
		delete iter->second;
	}	
}

bool page_lookup_predicate(Page *page1, Page *page2) //predicate for page searching
{
	return (page1->get_address() == page2->get_address());
}

/*
	This function uses the std::search() function to look up pages in 
	the page deque.	
*/
std::deque<Page*>::iterator *page_lookup(std::deque<Page*> *page_list, Page *target)
{
	std::deque<Page*>::iterator *ret = new std::deque<Page*>::iterator(); //allocate new iterator on heap
	Page *search_target[] = {target};

	//call the ST library's search() method
	*ret = std::search(
                       page_list->begin(),
                       page_list->end(),
                       search_target,
                       search_target+1,
                       page_lookup_predicate
			          );

	return ret;
}

/*
	This function is called during a page fault to update the system's 
	statistics on which pages qualify as "least recently used". For each 
	of the 4 types, there exists a deque container from which a page is randomly 
	selected for replacement once the lowest existing subtype is ascertained.
*/
void update_nru_type_counts()
{
	//empty the containers
	nru_type_3_list.clear();
	nru_type_2_list.clear();
	nru_type_1_list.clear();
	nru_type_0_list.clear();

	for (std::deque<Page*>::iterator page_iterator = addr_table.begin(); page_iterator != addr_table.end(); page_iterator++)
	{
		if ((*page_iterator)->get_usage_status() == 3)
		{
			nru_type_3_list.push_front(std::pair<int, Page*> ((int)std::distance(addr_table.begin(), page_iterator), *page_iterator));
		}
		else if ((*page_iterator)->get_usage_status() == 2)
		{
			nru_type_2_list.push_front(std::pair<int, Page*> ((int)std::distance(addr_table.begin(), page_iterator), *page_iterator));
		}
		else if ((*page_iterator)->get_usage_status() == 1)
		{
			nru_type_1_list.push_front(std::pair<int, Page*> ((int)std::distance(addr_table.begin(), page_iterator), *page_iterator));
		}
		else //((*page_iterator)->get_usage_status() == 0)
		{
			nru_type_0_list.push_front(std::pair<int, Page*> ((int)std::distance(addr_table.begin(), page_iterator), *page_iterator));
		}
	}
}

/* 
   a predicate functor for comparison of address occurences for use in computing
   the optimal replacement page via the Standard Template Library's std::search() function
*/
bool optimal_distance_predicate(int i, int j)
{
	return (i>j);
}

/*
	This function computes the lowest distance GREATER THAN the current location in the
	address stream. The values returned from this function are the ones that are compared
	when searching for the optimal page to replace.
*/
int get_optimal_distance(std::deque<int> *distance_list, int current_location)
{
	int ret = -1;	
	int search_target[] = {current_location};
	std::deque<int>::iterator distance_iterator = distance_list->begin();

	distance_iterator = std::search(
		                            distance_iterator, 
		                            distance_list->end(), 
		                            search_target, 
		                            search_target+1, 
		                            optimal_distance_predicate
		                           );

	if (distance_iterator != distance_list->end())
	{
		ret = ((*distance_iterator) - current_location);
	}

	return ret;
}





















