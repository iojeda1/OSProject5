/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h> 
 
int *frame_table; 
int *frame_page;
int nframesg;
int npagesg; 
char *algo; 

int page_faults = 0;
int reads = 0; 
int writes = 0; 

int hand = 0; 

/* Global pointer to the disk object */
struct disk *disk = 0;

/* Global pointer to the physical memory spacet */
unsigned char *physmem = 0;

/* Global pointer to the virtual memory spacet */
unsigned char *virtmem = 0;

/* A dummy page fault handler to start.  This is where most of your work goes. */
void page_fault_handler( struct page_table *pt, int page )
{
    page_faults++; 
    int frame; // frame number  
    int bits;
    page_table_get_entry(pt, page, &frame, &bits); // lookup current mapping and permissions of page 
    int free = -1; // flag 

    if (bits & BIT_PRESENT) { // in memory but no write permission
        page_table_set_entry(pt,page,frame,bits|BIT_WRITE); // grant write access 
        return; 
	}
    for (int i = 0; i < nframesg; i++) { // find a free physical frame and store it 
        if (frame_table[i] == -1) {
            free = i; 
            break; 
        }
    }
    int victim; 
    int page_victim = -1; 
    if (free != -1) { // frame is free, found our victim
        victim = free; 
    } else { // no free frame, so choose algorithm 
        if (strcmp(algo, "rand") == 0) { 
            while (1) {
                victim = rand() % nframesg; // random victim 
                if (frame_table[victim] != -1) {
                    page_victim = frame_table[victim];
                    break;
                }
            } 
        } else if (strcmp(algo, "clock") == 0) {
            while (1) {
                int c_page = frame_table[hand]; 
                int c_bits; 
                int temp_frame; 
                page_table_get_entry(pt, c_page, &temp_frame, &c_bits); // get clock hand's persmissions
                if (c_bits & BIT_REF) { // if page was recently used, clear, advance, continue 
                    page_table_set_entry(pt, c_page, temp_frame, c_bits & ~BIT_REF);
                    hand = (hand+1) % nframesg; // move the clock hand 
                } else { // if the page has not been used recently, record for eviction  
                    victim = hand;
                    page_victim = c_page; 
                    hand = (hand+1) % nframesg; // move clock hand 
                    break;  
                }
            }
        } else if (strcmp(algo, "custom")== 0) { 
		int found = 0;
        	int scanned = 0;
		int rounds = 0;
		int max_scans = 2 * nframesg;
		
		while (!found && scanned < max_scans) {
			int c_page = frame_table[hand];
        		int c_bits;
        		int temp_frame;
        		page_table_get_entry(pt, c_page, &temp_frame, &c_bits);

        		int is_ref = c_bits & BIT_REF;
        		int is_dirty = c_bits & BIT_DIRTY;

			scanned++;
			hand = (hand + 1) % nframesg;

        	// First pass: evict clean unreferenced
        	if (rounds == 0 && !is_ref && !is_dirty) {
            		victim = hand;
            		page_victim = c_page;
            		found = 1;
        	}
        	// Second pass: fallback to regular clock
        	else if (rounds == 1 && !is_ref) {
            		victim = hand;
            		page_victim = c_page;
            		found = 1;
        	} else {
                // Clear REF bit and continue
            		page_table_set_entry(pt, c_page, temp_frame, c_bits & ~BIT_REF);
            		hand = (hand + 1) % nframesg;
        	}

        	// If we finished one full pass and didn't find a clean page, try round 2
        	if (!found && hand == 0) {
            		rounds++;
        	}
    	    }

	} else {
            fprintf(stderr, "Error: unknown algorithm \n");
            exit(1); 
        }    
	if (page_victim != -1) { 
            int bits_victim; 
            int temp_frame; 
            page_table_get_entry(pt,page_victim,&temp_frame,&bits_victim);
            if (bits_victim & BIT_DIRTY) { // if page was modified, write it back to disk 
                disk_write(disk, page_victim, &physmem[victim * PAGE_SIZE]);
                writes++; 
            }
            // clean up
            page_table_set_entry(pt,page_victim,0,0);
            frame_page[page_victim] = -1; 
         }
       }
    disk_read(disk, page, &physmem[victim * PAGE_SIZE]);
    reads++; 
    // make necessary updates 
    page_table_set_entry(pt, page, victim, BIT_PRESENT | BIT_WRITE);
    frame_table[victim] = page; 
    frame_page[page] = victim; 
}

int main( int argc, char *argv[] )
{
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|clock|custom> <alpha|beta|gamma|delta>\n");
		return 1;
	}

	int npages = atoi(argv[1]);
	int nframes = atoi(argv[2]);
	const char *algoname = argv[3];
	const char *program = argv[4];

	disk = disk_open("myvirtualdisk",npages);
	if(!disk) {
		fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
		return 1;
	}

	struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
	if(!pt) {
		fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
		return 1;
	}
	physmem = page_table_get_physmem(pt);
	virtmem = page_table_get_virtmem(pt);
	
    srand(time(NULL)); 
    nframesg = nframes; 
    npagesg = npages; 
    algo = strdup(algoname); 
    frame_table = malloc(sizeof(int) * nframes);
    frame_page = malloc(sizeof(int) * npages);
    for (int i = 0; i < nframes; i++) {
        frame_table[i] = -1;
    } 
    for (int i = 0; i < npages; i++) {
        frame_page[i] = -1;
    }

	if(!strcmp(program,"alpha")) {
		alpha_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"beta")) {
		beta_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"gamma")) {
		gamma_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"delta")) {
		delta_program(pt,virtmem,npages*PAGE_SIZE);

	} else {
		fprintf(stderr,"unknown program: %s\n",argv[4]);
		return 1;
	}

    // DEBUG 
    printf("Page faults: %d\n", page_faults);
    printf("Disk reads:  %d\n", reads);
    printf("Disk writes: %d\n", writes);
    free(frame_table);
    free(frame_page);
    free(algo);
    page_table_delete(pt);
    disk_close(disk);

    return 0;
}
