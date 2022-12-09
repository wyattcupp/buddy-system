
/**
 * Implementation of a memory allocator based on the Buddy System.
 * See Knuth Art of Computer Programming, vol. 1, page 442. 
 * Each available block starts with a header that consists
 * of a tag (free/reserved), kval (size of block 2^kval), next
 * and previous pointers. Each reserved block has the tag and kval 
 * field only. All allocations are done in powers of two. All requests
 * are rounded up to the next power of two.
 * 
 * @author Wyatt Cupp
 * 
 */
 
#include "buddy.h"
#include <stdint.h>

static int initialized = FALSE; // used for buddy_init flag

/**
 *  Gets the Log2 (kval) of given raw size.
 * 
 * */ 
static unsigned short int get_kval(size_t size) {
	//TODO: limit kval min?
	size_t kval = 1;
	size_t curr = 1;
	while (curr<size) {
		curr <<= 1;
		kval++;
	}

	return kval-1;
}


/* the header for an available block */
struct block_header {
	short tag;
	short kval;
	struct block_header *next;
	struct block_header *prev;
};

const int RESERVED = 0;
const int FREE = 1;
const int UNUSED = -1; /* useful for header nodes */


/* supports memory upto 2^(MAX_KVAL-1) (or 64 GB) in size */
#define  MAX_KVAL  37
#define MAX_SIZE ((size_t) 1 << (MAX_KVAL-1))


/* default memory allocation is 512MB */
const size_t DEFAULT_MAX_MEM_SIZE = 512*1024*1024;


/* A static structure stores the table of pointers to the lists in the buddy system.  */
struct pool {
	void *start; // pointer to the start of the memory pool
	int lgsize;  // log2 of size
	size_t size; // size of the pool, same as 2 ^ lgsize
	/* the table of pointers to the buddy system lists */
	struct block_header avail[MAX_KVAL];
} pool;


/* initialize structures */

static struct pool mempool;

int buddy_init(size_t size) {
	// check if size > max available
	if (size > MAX_SIZE) {
		errno=ENOMEM;
		return errno;
	}

	void *ptr;
	// check for default initialization
	if (size==0) {
		ptr = (void *) sbrk(DEFAULT_MAX_MEM_SIZE);
		mempool.size = DEFAULT_MAX_MEM_SIZE;
	}else {
		// round to next power of 2 (unchanged if not)
		unsigned short int kval = get_kval(size);
		size = (UINT64_C(1) <<kval);
		ptr = (void *) sbrk(size);
		mempool.size = size;
	}

	// check if sbrk failed:
	if(ptr == (void *)-1) {
		errno = ENOMEM;
		return errno;
	}

	mempool.start = (void *) ptr; // sets start address for memory block

	// find logsize/kval of mempool (log2):	
	unsigned short int kval = get_kval(mempool.size);

	// set the rest of mempool variables and create initial block_header
	mempool.lgsize = kval;	
	
	size_t i = 0;
	// create block headers up to kval index
	for(i = 0; i < kval; i++) {
		mempool.avail[i].next = mempool.avail[i].prev = &mempool.avail[i]; // set next and prev pointers to self for malloc algorithm
		mempool.avail[i].kval = i; // set kval to curr.
		mempool.avail[i].tag = UNUSED; 
	}

	// set kval index block header
	mempool.avail[kval].next = mempool.avail[kval].prev = (struct block_header *)mempool.start;
	mempool.avail[kval].next->tag = FREE;
	mempool.avail[kval].next->kval = kval;
	mempool.avail[kval].next->next = mempool.avail[kval].next->prev = &mempool.avail[kval];

	initialized = TRUE;
    return TRUE;
}


void *buddy_malloc(size_t size)
{
	// check if budddy init has already been called:
	if (initialized==FALSE) {
		if(buddy_init(0) != TRUE) {
			errno=ENOMEM;
			return NULL;
		}
		initialized = TRUE;
	}

	// first, find kval of current size.
	unsigned short int kval = get_kval(sizeof(struct block_header)+size);

	if(kval > mempool.lgsize) {
		//error
		errno = ENOMEM;
		return NULL;
	}

	/* Now we begin following Algorithm R (Buddu system reservation) as closely as possible */

	//1. (find block): let j be the smallest int in range k <=j<= m in which AVAILF[j] != LOC(AVAIL[j]
	struct block_header *free_block = NULL;
	unsigned short int j = kval;

	while(j <= mempool.lgsize && (free_block == NULL)) { // smallest in range kval <= j <= mempool.lgsize
		if(mempool.avail[j].next != &mempool.avail[j]) { // AVAILF[j] != LOC(AVAIL[j]), or, avail.next != address of avail[j]
			free_block = mempool.avail[j].next;
			break;
		}
		j++;
	}

	if(free_block == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	j = free_block->kval;

	//2. (remove from list): set L=AVAILF[j], P=LINKF(L), AVAILF[j] = P, LINKB(P) = LOC(AVAIL[j]) and TAG(L)=0
	
	struct block_header *L = mempool.avail[j].next;
	struct block_header *P = L->next;
	mempool.avail[j].next = P;
	P->prev = &mempool.avail[j];
	L->tag = RESERVED;
	L->kval = kval;

	//3. Check if split is required: If j=k, terminate(we have found and reserved an available block at address L)

	//4. Split: Decrement j, set P=L+2^j, Tag(P)=1, kval(P)=j, LINKF(P)=LINKB(P)=LOC(AVAIL[j]), AVAILF[j]=AVAILB[j]=P.
	while(j!=kval) {
		j--;
		struct block_header *P = (struct block_header *) (((uint_least64_t) L) + (UINT64_C(1) << j));
		P->tag = FREE;
		P->kval = j;
		P->next = P->prev = &mempool.avail[j];
		mempool.avail[j].next = mempool.avail[j].prev = P;
	}

	return L+1;
}


void *buddy_calloc(size_t nmemb, size_t size) 
{	
	// get address from malloc
	void *addr = buddy_malloc(size * nmemb);

	// set memory to zeros using memset
	memset(addr, 0, (nmemb * size));

	// return addr of calloc
	return addr;
}

void *buddy_realloc(void *ptr, size_t size) 
{
	if(ptr==NULL && size==0) {
		errno = ENOMEM;
		return NULL; // return errno???
	}

    //if size==0, equivalent to buddy_free(ptr)
    if (size == 0) {
        buddy_free(ptr);
        return NULL;
    }

    // ptr==Null, equivalent to malloc(size)
	if (ptr == NULL) {
        return buddy_malloc(size);
    }


    // get block pointed to by ptr:
    struct block_header *block = (struct block_header *)ptr - 1;

    // get kval from block pointed to by ptr:
    unsigned short int kval = get_kval(size + sizeof(struct block_header));

    // check if kval is already pointing to block-kval:
    if (kval == block->kval) {
        return ptr;
    }

    // malloc necessary size, memcpy addr, free ptr:
    void *addr = buddy_malloc(size);
    memcpy(addr, ptr, size);
    buddy_free(ptr);

    return addr;
}

/**
 * Finds the buddy from current block header L.
 * */
static struct block_header* find_buddy(struct block_header * L) {
	
	// unsigned short int k = L->kval;

	uint_least64_t buddy_a = ((uint_least64_t) L) - ((uint_least64_t) mempool.start);
	uint_least64_t mask = UINT64_C(1) << L->kval;

	// create pointer to buddy_b after flipping kbit and aligning with start addr:
	struct block_header* buddy_b = (struct block_header *) ((buddy_a ^ mask) + ((uint_least64_t) mempool.start));

	return buddy_b;
}


void buddy_free(void *ptr) 
{
	if(ptr == NULL || !initialized) {
		return;
	}

	/* Follow from the Art of Computer programming p. 443-444 */
	// 1. [is buddy available?] set P = buddy_k(L) if k=m or tag(P)=0,1 and KVAL(P) != k, SKIP TO STEP 3
	struct block_header *L = (struct block_header *)ptr-1; // current buddy L (returned from malloc-1 addr)
	unsigned short int kval = L->kval; 

	//  while (1. buddy is NOT available): 2. combine with buddy
	while(TRUE) {

		struct block_header *buddy = find_buddy(L); // finds the buddy of L (current block from *ptr)

		if(kval == mempool.lgsize || buddy->tag == RESERVED || (buddy->tag == FREE && buddy->kval != kval)){
			//3. [put on list]
			L->tag = FREE;
			buddy = mempool.avail[kval].next;	
			L->next = buddy;
			buddy->prev = L;
			L->kval = kval;
			L->prev = &mempool.avail[kval];
			mempool.avail[kval].next = L;		
			break;
		}

		buddy->prev->next = buddy->next;
		buddy->next->prev = buddy->prev;

		kval++;

		if(buddy < L) {
			L = buddy;
		}

		L->kval = kval;
	}
}


void printBuddyLists()
{
	int i;
	int free_blocks = 0;

	// loop through AVAIL[MAX_KVAL]
	for(i = 0; i <= mempool.lgsize; i++) {
		printf("List %d: head = %p", i, &mempool.avail[i]);

		struct block_header *curr = mempool.avail[i].next;

		while(curr != &mempool.avail[i]) {
			if(curr->tag==FREE) {free_blocks++;}

			printf(" --> [tag=%d, kval=%d, addr=%p]", curr->tag, curr->kval, curr->next);

			curr = curr->next;
		}

		printf(" --> <null>\n");
	}
	printf("\n Free Blocks: %d\n", free_blocks);
}

