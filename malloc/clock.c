/* 
 * clock.c - Routines for using the cycle counters on x86, 
 *           Alpha, AArch64, and Sparc boxes.
 * 
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/times.h>
#include "clock.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif


/******************************************************* 
 * Machine dependent functions 
 *
 * Note: the constants __i386__, __x86_64__, __alpha,
 * __aarch64__, and __arm64__ are set by the compiler
 * when it calls the C preprocessor.
 *******************************************************/

#if defined(__i386__) || defined(__x86_64__)
/*******************************************************
 * Pentium versions of start_counter() and get_counter()
 *******************************************************/


/* $begin x86cyclecounter */
/* Initialize the cycle counter */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* Set *hi and *lo to the high and low order bits  of the cycle counter.  
   Implementation requires assembly code to use the rdtsc instruction. */
void access_counter(unsigned *hi, unsigned *lo)
{
    asm("rdtsc; movl %%edx,%0; movl %%eax,%1"   /* Read cycle counter */
        : "=r" (*hi), "=r" (*lo)                /* and move results to */
        : /* No input */                        /* the two outputs */
        : "%edx", "%eax");
}

/* Record the current value of the cycle counter. */
void start_counter()
{
    access_counter(&cyc_hi, &cyc_lo);
}

/* Return the number of cycles since the last call to start_counter. */
double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;

    /* Get cycle counter */
    access_counter(&ncyc_hi, &ncyc_lo);

    /* Do double precision subtraction */
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
        fprintf(stderr, "Error: counter returns neg value: %.0f\n", result);
    }
    return result;
}
/* $end x86cyclecounter */

#elif defined(__aarch64__) || defined(__arm64__)
/*******************************************************
 * AArch64 versions of start_counter() and get_counter()
 *
 * Uses the ARM virtual count register (cntvct_el0)
 * accessible from userspace on AArch64.
 *******************************************************/

static unsigned long long cyc_start = 0;

/* Read the AArch64 virtual cycle counter */
static inline unsigned long long read_cntvct(void)
{
    unsigned long long val;
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));
    return val;
}

/* Record the current value of the cycle counter. */
void start_counter()
{
    cyc_start = read_cntvct();
}

/* Return the number of cycles since the last call to start_counter. */
double get_counter()
{
    unsigned long long now = read_cntvct();
    double result = (double)(now - cyc_start);
    if (result < 0) {
        fprintf(stderr, "Error: counter returns neg value: %.0f\n", result);
    }
    return result;
}

#elif defined(__alpha)

/****************************************************
 * Alpha versions of start_counter() and get_counter()
 ***************************************************/

/* Initialize the cycle counter */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* Use Alpha cycle timer to compute cycles.  Then use
   measured clock speed to compute seconds 
*/

/*
 * counterRoutine is an array of Alpha instructions to access 
 * the Alpha's processor cycle counter. It uses the rpcc 
 * instruction to access the counter. This 64 bit register is 
 * divided into two parts. The lower 32 bits are the cycles 
 * used by the current process. The upper 32 bits are wall 
 * clock cycles. These instructions read the counter, and 
 * convert the lower 32 bits into an unsigned int - this is the 
 * user space counter value.
 * NOTE: The counter has a very limited time span. With a 
 * 450MhZ clock the counter can time things for about 9 
 * seconds. */
static unsigned int counterRoutine[] =
    {
        0x601fc000u,
        0x401f0000u,
        0x6bfa8001u
    };

/* Cast the above instructions into a function. */
static unsigned int (*counter)(void)= (void *)counterRoutine;


void start_counter()
{
    /* Get cycle counter */
    cyc_hi = 0;
    cyc_lo = counter();
}

double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;
    ncyc_lo = counter();
    ncyc_hi = 0;
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
        fprintf(stderr, "Error: Cycle counter returning negative value: %.0f\n", result);
    }
    return result;
}

#else

/****************************************************************
 * All the other platforms for which we haven't implemented cycle
 * counter routines. Newer models of sparcs (v8plus) have cycle
 * counters that can be accessed from user programs, but since there
 * are still many sparc boxes out there that don't support this, we
 * haven't provided a Sparc version here.
 ***************************************************************/

void start_counter()
{
    printf("ERROR: You are trying to use a start_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}

double get_counter() 
{
    printf("ERROR: You are trying to use a get_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}
#endif




/*******************************
 * Machine-independent functions
 ******************************/
double ovhd()
{
    /* Do it twice to eliminate cache effects */
    int i;
    double result;

    for (i = 0; i < 2; i++) {
        start_counter();
        result = get_counter();
    }
    return result;
}

/* $begin mhz */
/* Get the clock rate from /proc or sysctl */
double mhz_full(int verbose, int sleeptime __attribute__((unused)))
{
    double mhz = 0.0;

#if defined(__APPLE__)
    /* macOS: use sysctl to get CPU frequency.
     * On Apple Silicon, hw.cpufrequency may not be available,
     * so fall back to the counter frequency for cntvct_el0. */
    uint64_t freq = 0;
    size_t size = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &size, NULL, 0) == 0) {
        mhz = (double)freq / 1e6;
    }
#if defined(__aarch64__) || defined(__arm64__)
    /* On Apple Silicon, hw.cpufrequency is often unavailable.
     * Use cntfrq_el0 (the ARM counter frequency) instead, since
     * our cycle counter reads cntvct_el0 which ticks at this rate. */
    if (mhz == 0.0) {
        unsigned long long cntfrq;
        asm volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
        mhz = (double)cntfrq / 1e6;
    }
#endif
    /* Final fallback */
    if (mhz == 0.0)
        mhz = 2000;
#else
    /* Linux: read from /proc/cpuinfo */
    static char buf[2048];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(buf, 2048, fp)) {
            if (strstr(buf, "cpu MHz")) {
                sscanf(buf, "cpu MHz\t: %lf", &mhz);
                break;
            }
        }
        fclose(fp);
    }
    if (mhz == 0.0)
        mhz = 2000;
#endif

    if (verbose) 
        printf("Processor clock rate ~= %.1f MHz\n", mhz);
    return mhz;

#if 0
    double rate;

    start_counter();
    sleep(sleeptime);
    rate = get_counter() / (1e6*sleeptime);
    if (verbose) 
        printf("Processor clock rate ~= %.1f MHz\n", rate);
    return rate;
#endif
}
/* $end mhz */

/* Version using a default sleeptime */
double mhz(int verbose)
{
    return mhz_full(verbose, 2);
}

/** Special counters that compensate for timer interrupt overhead */

static double cyc_per_tick = 0.0;

#define NEVENT 100
#define THRESHOLD 1000
#define RECORDTHRESH 3000

/* Attempt to see how much time is used by timer interrupt */
static void callibrate(int verbose)
{
    double oldt;
    struct tms t;
    clock_t oldc;
    int e = 0;

    times(&t);
    oldc = t.tms_utime;
    start_counter();
    oldt = get_counter();
    while (e <NEVENT) {
        double newt = get_counter();

        if (newt-oldt >= THRESHOLD) {
            clock_t newc;
            times(&t);
            newc = t.tms_utime;
            if (newc > oldc) {
                double cpt = (newt-oldt)/(newc-oldc);
                if ((cyc_per_tick == 0.0 || cyc_per_tick > cpt) && cpt > RECORDTHRESH)
                    cyc_per_tick = cpt;
                /*
                  if (verbose)
                  printf("Saw event lasting %.0f cycles and %d ticks.  Ratio = %f\n",
                  newt-oldt, (int) (newc-oldc), cpt);
                */
                e++;
                oldc = newc;
            }
            oldt = newt;
        }
    }
    if (verbose)
        printf("Setting cyc_per_tick to %f\n", cyc_per_tick);
}

static clock_t start_tick = 0;

void start_comp_counter() 
{
    struct tms t;

    if (cyc_per_tick == 0.0)
        callibrate(0);
    times(&t);
    start_tick = t.tms_utime;
    start_counter();
}

double get_comp_counter() 
{
    double time = get_counter();
    double ctime;
    struct tms t;
    clock_t ticks;

    times(&t);
    ticks = t.tms_utime - start_tick;
    ctime = time - ticks*cyc_per_tick;
    /*
      printf("Measured %.0f cycles.  Ticks = %d.  Corrected %.0f cycles\n",
      time, (int) ticks, ctime);
    */
    return ctime;
}

