#include "heap.h"

// Implement a heap in an array. The root node is stored at 0-indexed i = 1.

/**
 * @brief swap this node with its parent until it reaches the root or its parent
 * fulfills the min heap condition.
 * 
 * @param pHeap pointer to heap array
 * @param i index into heap array, 1 is root.
 */
void minHeapBubbleUp(double* pHeap, uint32_t i)
{
    // Recursion end condition:
    // - Reached root node
    // - Satisfied min-heap condition
    if ((1 == i) || (pHeap[i / 2] < pHeap[i])) {
        return;
    }
    if (pHeap[i] < pHeap[i / 2]) {
        SWAP(pHeap[i], pHeap[i / 2]);
    }
    minHeapBubbleUp(pHeap, i / 2);
    return;
}



/**
 * @brief move a misplaced node down in the tree until the min heap condition
 * is satisfied
 * 
 * @param pHeap pointer to heap array
 * @param heapSize current size of heap, exclusive of 0th dummy element
 * @param i 
 */
void minHeapBubbleDown(double* pHeap, uint32_t heapSize, uint32_t i)
{
    uint32_t newIdx = 1;
    // Don't fall off the end
    if ((i * 2) > heapSize) {
        return;
    }
    if (((i * 2) + 1) > heapSize) {
        newIdx = i * 2;
    } else {
        // Swap current node with smaller child until there are none left
        double node = pHeap[i];
        double lChild = pHeap[i * 2];
        double rChild = pHeap[(i * 2) + 1];
        if ((node > lChild) || (node > rChild)) {
            if (lChild < rChild) {
                newIdx = i * 2;
            } else {
                newIdx = (i * 2) + 1;
            }
            SWAP(pHeap[i], pHeap[newIdx]);
            minHeapBubbleDown(pHeap, heapSize, newIdx);
        }
    }
    return;
}


/**
 * @brief remove root node by overwriting it with a last layer node, and
 * bubbling down until the heap condition is satisfied
 * 
 * @param pHeap pointer to heap array
 * @param idxLast index of the last valid heap element. For a 1-indexed heap
 * with dummy 0th element, the last index and size of the heap are the same.
 */
void minHeapRemoveRoot(double* pHeap, uint32_t idxLast)
{
    pHeap[1] = pHeap[idxLast];
    minBubbleDown(pHeap, idxLast, 1);
    return;
}


/**
 * @brief insert a value in the lowest layer and bubble it up until the heap
 * condition is satisfied
 * 
 * @param pHeap 
 * @param[in,out] pIdxLast
 * @param val 
 */
void minHeapInsert(double* pHeap, uint32_t* pIdxLast, double val)
{
    (*pIdxLast)++;
    pHeap[*pIdxLast] = val;
    minHeapBubbleUp(pHeap, *pIdxLast);
    return;
}


// # do insertion, but heap is fixed size
// def minheap_guarded_insert(heap, val, max_elem=10):
//     # don't insert any values smaller than the root of a min heap
//     if (len(heap) > 1) and (val < heap[1]):
//         return heap
//     # maintain the maximum heap size: pop heap min if necessary
//     if ((len(heap) - 1) >= max_elem):
//         minheap_remove_root(heap)
//     return heap_insert(heap, val, min_bubble_up)

void minHeapGuardedInsert(double* pHeap, uint32_t* pIdxLast, double val)
{
    // Don't insert any values smaller than the root into a min heap
}

