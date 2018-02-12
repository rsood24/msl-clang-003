/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"
#include <stdbool.h>
/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _alloc {
    char *mem;
    size_t size;
} alloc_t, *alloc_pt;

typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate

    if( pool_store == NULL)
    {
        pool_store = (pool_mgr_pt*)calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;

        return ALLOC_OK;
    }
    else
    {
        return ALLOC_CALLED_AGAIN;
    }
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables

    if(pool_store != NULL)
    {
       for(int i = 0; i < pool_store_capacity; ++i)
        {

            if(pool_store[i] != NULL)
                return ALLOC_FAIL;

        }
        free(pool_store);
        pool_store = NULL;
        pool_store_capacity = 0;
        pool_store_size = 0;
        return ALLOC_OK;
    }
    else
    {
        return ALLOC_CALLED_AGAIN;
    }


}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    if(pool_store == NULL)
        return NULL;
    pool_mgr_pt myMgr= (pool_mgr_pt) malloc(sizeof(pool_mgr_t));
    // expand the pool store, if necessary
    // allocate a new mem pool mgr
    if(myMgr == NULL)
        return NULL;
    // check success, on error return null

    myMgr->pool.mem = calloc(1, size);
    myMgr->pool.policy = policy;
    // allocate a new memory pool

    if(myMgr->pool.mem == NULL)
    {
        free(myMgr);
        myMgr = NULL;
        return NULL;
    }
    // check success, on error deallocate mgr and return null

    myMgr->node_heap = (node_pt)calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
    // allocate a new node heap

    if(myMgr->node_heap == NULL)
    {
        free(myMgr->pool.mem);
        free(myMgr);
        myMgr = NULL;
        return NULL;
    }
    // check success, on error deallocate mgr/pool and return null

    myMgr->gap_ix = (gap_pt)calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    // allocate a new gap index

    if(myMgr->gap_ix == NULL)
    {
        free(myMgr->pool.mem);
        free(myMgr->node_heap);
        free(myMgr);
        myMgr = NULL;
        return NULL;
    }
    // check success, on error deallocate mgr/pool/heap and return null

    myMgr->node_heap->alloc_record.mem = myMgr->pool.mem;
    myMgr->node_heap->alloc_record.size = size;
    myMgr->node_heap->used = 1;
    myMgr->node_heap->allocated = 0;
    myMgr->used_nodes = 1;
    myMgr->pool.total_size = size;
    myMgr->pool.alloc_size = 0;
    myMgr->pool.num_allocs = 0;
    myMgr->pool.num_gaps = 0;
    myMgr->total_nodes = 1;
    _mem_add_to_gap_ix(myMgr, size, myMgr->node_heap);
    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    //   initialize top node of gap index
    //   initialize pool mgr
    //   link pool mgr to pool store
    // return the address of the mgr, cast to (pool_pt)


    pool_store[pool_store_size] = myMgr;
    pool_store_size++;


    return (pool_pt) myMgr;
}

alloc_status mem_pool_close(pool_pt pool) {

    pool_mgr_pt aMgr = (pool_mgr_pt) pool;
    // get mgr from pool by casting the pointer to (pool_mgr_pt)

    if(pool == NULL)
        return ALLOC_NOT_FREED;
    // check if this pool is allocated

    if(pool->num_gaps > 1)
        return ALLOC_NOT_FREED;
    // check if pool has only one gap

    if(pool->num_allocs > 0)
        return ALLOC_NOT_FREED;
    // check if it has zero allocations

    free(pool->mem);
    free(pool);
    // free memory pool


    free(aMgr->node_heap);
    // free node heap

    free(aMgr->gap_ix);
    // free gap index

    int p = 0;
    while(pool_store[p] != aMgr)
    {
        ++p;
    }
    pool_store[p] = NULL;
    // find mgr in pool store and set to null
    // note: don't decrement pool_store_size, because it only grows

    //free(aMgr);
    // free mgr

    return ALLOC_OK;
}

void * mem_new_alloc(pool_pt pool, size_t size) {

    pool_mgr_pt aMgr = (pool_mgr_pt) pool;
    // get mgr from pool by casting the pointer to (pool_mgr_pt)

    if(aMgr->gap_ix == NULL)
        return NULL;
    // check if any gaps, return null if none


    // expand heap node, if necessary, quit on error
    // check used nodes fewer than total nodes, quit on error
    // get a node for allocation:

    node_pt aNode = NULL;
    int p = 0;
    if(pool->policy == FIRST_FIT)
    {
        while(aMgr->node_heap[p].alloc_record.size < size && aMgr->node_heap[p].allocated == 0)
        {
            p++;
        }
        aNode = &aMgr->node_heap[p];
    }
    else if(pool->policy == BEST_FIT)
    {
        while(aMgr->gap_ix[p].size < size)
        {
            p++;
        }
        aNode = aMgr->gap_ix[p].node;
    }
    // if FIRST_FIT, then find the first sufficient node in the node heap
    // if BEST_FIT, then find the first sufficient node in the gap index

    if(aNode->alloc_record.size < size)
    {
        return NULL;
    }
    // check if node found

    pool->num_allocs++;
    pool->alloc_size += size;
    // update metadata (num_allocs, alloc_size)

    unsigned gapSize = aNode->alloc_record.size - size;
    // calculate the size of the remaining gap, if any

    _mem_remove_from_gap_ix(aMgr, size, aNode);
    // remove node from gap index

    aNode->alloc_record.size = size;
    aNode->allocated = 1;
    // convert gap_node to an allocation node of given size

    // adjust node heap:
    //   if remaining gap, need a new node
    if(gapSize > 0)
    {
        int i = 0;
        while(aMgr->node_heap[i].used != 0)
        {
            ++i;
        }
        //   find an unused one in the node heap
        //   make sure one was found

        aMgr->node_heap[i].used = 1;
        aMgr->node_heap[i].allocated = 0;
        aMgr->node_heap[i].alloc_record.size = gapSize;
        aMgr->used_nodes++;
        //   initialize it to a gap node
        //   update metadata (used_nodes)

        node_pt temp = aNode->next;
        aNode->next = &aMgr->node_heap[i];
        aMgr->node_heap[i].prev = aNode;
        aMgr->node_heap[i].next = temp;
        //   update linked list (new node right after the node for allocation)

        _mem_add_to_gap_ix(aMgr, gapSize, &aMgr->node_heap[i]);
        //   add to gap index
        //   check if successful
        // return allocation record by casting the node to (alloc_pt)
    }

    return (alloc_pt) aNode;
}

alloc_status mem_del_alloc(pool_pt pool, void * alloc) {

    pool_mgr_pt aMgr = (pool_mgr_pt) pool;
    // get mgr from pool by casting the pointer to (pool_mgr_pt)

    node_pt aNode = (node_pt) alloc;
    // get node from alloc by casting the pointer to (node_pt)

    int i = 0;
    while(&aMgr->node_heap[i] != aNode)
    {
        ++i;
    }
    // find the node in the node heap
    // this is node-to-delete

    if(aMgr->node_heap[i].alloc_record.size != aNode->alloc_record.size)
    {
        return ALLOC_NOT_FREED;
    }
    // make sure it's found

    aMgr->node_heap[i].allocated = 0;
    // convert to gap node
    aMgr->pool.num_allocs--;
    aMgr->pool.alloc_size -= aNode->alloc_record.size;
    // update metadata (num_allocs, alloc_size)
    // if the next node in the list is also a gap, merge into node-to-delete
    node_pt ptrGap = NULL;
    if(aMgr->node_heap[i].next->allocated == 0 && aMgr->node_heap[i].next->used  == 1)
    {
        _mem_remove_from_gap_ix(aMgr, aMgr->node_heap[i].next->alloc_record.size, aMgr->node_heap[i].next);
        aMgr->node_heap[i].alloc_record.size += aMgr->node_heap[i].next->alloc_record.size;

        //   remove the next node from gap index
        //   check success
        //   add the size to the node-to-delete

        aMgr->node_heap[i].next->used = 0;
        aMgr->used_nodes--;
        //   update node as unused
        //   update metadata (used nodes)
        //   update linked list:



        if (aMgr->node_heap[i].next->next) {

            node_pt ptr = aMgr->node_heap[i].next;
            aMgr->node_heap[i].next->next->prev = &aMgr->node_heap[i];
            aMgr->node_heap[i].next = aMgr->node_heap[i].next->next;
            ptr->next = NULL;
            ptr->prev = NULL;

        } else {
            aMgr->node_heap[i].next = NULL;
        }
    }

    ptrGap = &aMgr->node_heap[i];


    // this merged node-to-delete might need to be added to the gap index

    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success

    if(aMgr->node_heap[i].prev != NULL && aMgr->node_heap[i].prev->allocated == 0 && aMgr->node_heap[i].prev->used  == 1)
    {
        ptrGap = aMgr->node_heap[i].prev;
        _mem_remove_from_gap_ix(aMgr, aMgr->node_heap[i].alloc_record.size, &aMgr->node_heap[i]);
        aMgr->node_heap[i].prev->alloc_record.size += aMgr->node_heap[i].alloc_record.size;

        //   add the size of node-to-delete to the previous
        //   update node-to-delete as unused
        //   update metadata (used_nodes)
        aMgr->node_heap[i].used = 0;
        aMgr->used_nodes--;
        //   update linked list

        if (aMgr->node_heap[i].next) {
            aMgr->node_heap[i].prev->next = aMgr->node_heap[i].next;
            aMgr->node_heap[i].next->prev = aMgr->node_heap[i].prev;
        } else {
            aMgr->node_heap[i].prev->next = NULL;
        }
        aMgr->node_heap[i].next = NULL;
        aMgr->node_heap[i].prev = NULL;

    }

    _mem_add_to_gap_ix(aMgr, ptrGap->alloc_record.size, ptrGap);
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    return ALLOC_OK;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {

    pool_mgr_pt aMgr = (pool_mgr_pt) pool;
    // get the mgr from the pool

    node_pt aNode = aMgr->node_heap;
    *num_segments = aMgr->used_nodes;
    *segments = (pool_segment_pt) calloc(aMgr->used_nodes, sizeof(pool_segment_t));
    int i = 0;
    while(aNode != NULL)
    {
        segments[i] = calloc(1, sizeof(pool_segment_t));
        segments[i]->size = aNode->alloc_record.size;
        segments[i]->allocated = aNode->allocated;
        aNode = aNode->next;
        ++i;
    }
    // allocate the segments array with size == used_nodes
    // check successful
    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    /*
                    *segments = segs;
                    *num_segments = pool_mgr->used_nodes;
     */

}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    /*
                if (((float) pool_store_size / pool_store_capacity)
                    > MEM_POOL_STORE_FILL_FACTOR) {...}
     */
    // don't forget to update capacity variables

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {

    // expand the gap index, if necessary (call the function)

    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
    // add the entry at the end

    pool_mgr->pool.num_gaps++;
    // update metadata (num_gaps)

    alloc_status status = _mem_sort_gap_ix(pool_mgr);
    // sort the gap index (call the function)

  //  if(status = ALLOC_FAIL)
  //       return NULL;

    // check success

    return ALLOC_OK;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    int i = 0;
    while(pool_mgr->gap_ix[i].node != node)
    {
        ++i;
    }
    // find the position of the node in the gap index

    while(i < pool_mgr->pool.num_gaps)
    {
        pool_mgr->gap_ix[i].size = pool_mgr->gap_ix[i+1].size;
        pool_mgr->gap_ix[i].node = pool_mgr->gap_ix[i+1].node;
        i++;
    }

    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = NULL;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = 0;

    pool_mgr->pool.num_gaps--;
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    // update metadata (num_gaps)
    // zero out the element at position num_gaps!

    return ALLOC_OK;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:

    bool swapped = true;
    int j = 0;
    size_t tmp;
    node_pt tmpNode;
    while (swapped) {

        swapped = false;
        j++;

        for (int i = 0; i < pool_mgr->pool.num_gaps - j; i++) {

            if (pool_mgr->gap_ix[i].size > pool_mgr->gap_ix[i + 1].size) {

                tmp = pool_mgr->gap_ix[i].size;
                tmpNode = pool_mgr->gap_ix[i].node;
                pool_mgr->gap_ix[i].size = pool_mgr->gap_ix[i + 1].size;
                pool_mgr->gap_ix[i].node = pool_mgr->gap_ix[i + 1].node;
                pool_mgr->gap_ix[i + 1].size = tmp;
                pool_mgr->gap_ix[i + 1].node = tmpNode;
                swapped = true;

            }
            else if(pool_mgr->gap_ix[i].size == pool_mgr->gap_ix[i + 1].size)
            {
                if(&pool_mgr->gap_ix[i].node > &pool_mgr->gap_ix[i + 1].node){

                    tmp = pool_mgr->gap_ix[i].size;
                    tmpNode = pool_mgr->gap_ix[i].node;
                    pool_mgr->gap_ix[i].size = pool_mgr->gap_ix[i + 1].size;
                    pool_mgr->gap_ix[i].node = pool_mgr->gap_ix[i + 1].node;
                    pool_mgr->gap_ix[i + 1].size = tmp;
                    pool_mgr->gap_ix[i + 1].node = tmpNode;
                    swapped = true;
                }
            }

        }

    }
        //    if the size of the current entry is less than the previous (u - 1)
        //    or if the sizes are the same but the current entry points to a
        //    node with a lower address of pool allocation address (mem)
        //       swap them (by copying) (remember to use a temporary variable)

    return ALLOC_OK;
}

static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr) {
    return ALLOC_FAIL;
}

