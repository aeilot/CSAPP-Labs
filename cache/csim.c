#include "cachelab.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

// Boolean
typedef enum { false, true } bool;

// Global Counters
int hit_count = 0;
int miss_count = 0;
int eviction_count = 0;

// Data Model
char* fileName = NULL;
int set_bit = -1;
long long sets = -1;
int associativity = -1;
int block_bit = -1;
long long block_size = -1;
bool verboseMode = false;

int global_timer = 0;

int memory_bit = 64; // Assuming 64-bit addresses
int tag_bit = 0; // Tag bits

// Cache Line Structure
typedef struct CacheLine {
    bool valid;
    long long tag;
    /*
        Need LRU stamp to implement LRU eviction policy
    */
    int lru_counter;
} CacheLine;

// Parse Line Structure
long long getTag(long long address) {
    return address >> (set_bit + block_bit);
}

long long getSetIndex(long long address) {
    long long mask = (1LL << set_bit) - 1;
    return (address >> block_bit) & mask;
}

long long getBlockOffset(long long address) {
    long long mask = (1LL << block_bit) - 1;
    return address & mask;
}

// Data Structures
CacheLine** cache = NULL;

// HELPER FUNCTIONS
void printUsage(char* argv[]) {
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h          Print this help message.\n");
    printf("  -v          Optional verbose flag.\n");
    printf("  -s <num>    Number of set index bits.\n");
    printf("  -E <num>    Number of lines per set.\n");
    printf("  -b <num>    Number of block offset bits.\n");
    printf("  -t <file>   Trace file.\n\n");
    printf("Examples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
}

void printArgs() {
    printf("Parsed args: s=%d E=%d b=%d t=%s v=%s\n",
           set_bit,
           associativity,
           block_bit,
           fileName ? fileName : "(null)",
           verboseMode ? "true" : "false");
}

void handleArgs(int argc, char** argv){
    int opt;

    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch(opt) {
            case 'h':
                printUsage(argv);
                exit(0);
            case 'v':
                verboseMode = true;
                break;
            case 't':
                fileName = optarg;
                break;
            case 's':
                set_bit = atoi(optarg);
                break;
            case 'E':
                associativity = atoi(optarg);
                break;
            case 'b':
                block_bit = atoi(optarg);
                break;
            case '?':
                printUsage(argv);
                exit(1);
            default:
                exit(1); 
        }
    }

    if(fileName == NULL || set_bit == -1 || associativity == -1 || block_bit == -1) {
        printf("Missing required command line argument");
        printUsage(argv);
        exit(1);
    }

    sets = 1LL << set_bit;
    block_size = 1LL << block_bit;
    
    tag_bit = memory_bit - (set_bit + block_bit);
}

// CACHE SIMULATION FUNCTIONS
void initCache() {
    // Initialize cache data structures
    cache = (CacheLine**) malloc(sizeof(CacheLine*) * sets);
    for(int i = 0; i<sets; i++){
        cache[i] = (CacheLine*) calloc(associativity, sizeof(CacheLine));
        /*
            malloc needs to be initialized
            otherwise the valid bits may contain garbage values
        */
        // Alternatively, we could use malloc + zero-initialize
        // cache[i] = (CacheLine*) malloc(sizeof(CacheLine) * associativity);
        // cache[i]->valid = false;
        // cache[i]->tag = 0;
        // cache[i]->lru_stamp = 0;
    }   
}

void freeCache() {
    // Free allocated memory for cache
    for(int i = 0; i<sets; i++) free(cache[i]);
    free(cache);
}

void loadData(long long address, int size) {
    // Simulate accessing data at the given address
int s = getSetIndex(address);
    long long t = getTag(address);
    global_timer++;

    for (int i = 0; i < associativity; i++) {
        if (cache[s][i].valid && cache[s][i].tag == t) {
            hit_count++;
            cache[s][i].lru_counter = global_timer;
            if (verboseMode) printf(" hit");
            return;
        }
    }

    miss_count++;
    if (verboseMode) printf(" miss");

    for (int i = 0; i < associativity; i++) {
        if (!cache[s][i].valid) {
            cache[s][i].valid = true;
            cache[s][i].tag = t;
            cache[s][i].lru_counter = global_timer; 
            return;
        }
    }

    eviction_count++;
    if (verboseMode) printf(" eviction");

    int victim_index = 0;
    int min_lru = cache[s][0].lru_counter;

    for (int i = 1; i < associativity; i++) {
        if (cache[s][i].lru_counter < min_lru) {
            min_lru = cache[s][i].lru_counter;
            victim_index = i;
        }
    }

    cache[s][victim_index].tag = t;
    cache[s][victim_index].lru_counter = global_timer;
}

void storeData(long long address, int size) {
    // Simulate storing data at the given address
    loadData(address, size);
}

void modifyData(long long address, int size) {
    // Simulate modifying data at the given address
    loadData(address, size);
    hit_count++;
    if (verboseMode) printf(" hit\n");
}

// MAIN FUNCTION
int main(int argc, char** argv)
{
    handleArgs(argc, argv);
    // Initialize Data Structures
    initCache();
    // Handle trace file
    FILE *traceFile = fopen(fileName, "r");
    if (traceFile == NULL) {
        printf("Error opening file: %s\n", fileName);
        exit(1);
    }
    char operation;
    long long address;
    int size;
    /*
        fscanf does not skip spaces before %c, so we add a space before %c in the format string
        !feof (traceFile) does not work correctly here because fscanf may fail to read a line
        but we are not at the end of the file yet. Instead, we check the return value of fscanf.
    */
    while (fscanf(traceFile, " %c %llx,%d", &operation, &address, &size) == 3) {
        switch (operation) {
            case 'L':
                // Handle load operation
                loadData(address, size);
                break;
            case 'S':
                // Handle store operation
                storeData(address, size);
                break;
            case 'M':
                // Handle modify operation
                modifyData(address, size);
                break;
            default:
                // Ignore other operations
                break;
        }
    }
    // Close trace file
    fclose(traceFile);
    // Free Data Structures
    freeCache();
    // Print Summary
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
