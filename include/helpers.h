#ifndef HELPERS_H
#define HELPERS_H

/* Helper function declarations go here */
#include "icsmm.h"
#include "debug.h"

#define COALESCEUP 1
#define COALESCEDOWN 1


int calculateBlockSize(int size);
int getCeil(double n);
ics_free_header* getFirstFreeBlock(ics_free_header* head, int requiredBlockSize);
void insert(ics_free_header** head, ics_free_header* header);
void removeNode(ics_free_header** head, ics_free_header* header);
void splinter(ics_free_header ** head, ics_header* startOfBlock, int fullBlockSize, int requiredBlockSize, int size);
int validatePtr(ics_header * ptr, void* prologue, void* epilogue, int allocBit);
void * coalesceUp(ics_free_header ** head,  ics_header * startOfBlock, void* prologue, void* epilogue);
void * coalesceDown(ics_free_header ** head,  ics_header * startOfBlock, void* prologue, void* epilogue);

#endif
#include <math.h>