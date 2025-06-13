#ifndef _HEAP_H
#define _HEAP_H

#include <stdint.h>

double itemp;
#define SWAP(a,b) itemp=(a);(a)=(b);(b)=itemp;

// Based on number of blobs useful for plate solving.
// astrometry.net performs just as well with 50 blobs as with more, we include
// more capacity to account for possible false positives.
#define HEAP_MAX_SIZE 300

void minHeapBubbleUp(double* pHeap, int i);
void minHeapBubbleDown(double* pHeap, uint32_t heapSize, uint32_t i);
void minHeapRemoveRoot(double* pHeap, uint32_t idxLast);
void minHeapInsert(double* pHeap, double val);

#endif