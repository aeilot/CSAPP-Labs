#include "cachelab.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

// Boolean
typedef enum { false, true } bool;

// Data Model
char* fileName = NULL;
int set_bit = -1;
int associativity = -1;
int block_bit = -1;
bool verboseMode = false;

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
}

int main(int argc, char** argv)
{
    handleArgs(argc, argv);
    printArgs();
    return 0;
}
