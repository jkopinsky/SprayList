#ifndef PRIOQ_H
#define PRIOQ_H

#include "linden_common.h"

typedef unsigned long pkey_t;
typedef unsigned long pval_t;

#define KEY_NULL 0
#define NUM_LEVELS 32
/* Internal key values with special meanings. */
#define SENTINEL_KEYMIN ( 0UL) /* Key value of first dummy node. */
#define SENTINEL_KEYMAX (~1UL) /* Key value of last dummy node.  */


typedef struct node_s
{
    pkey_t    k;
    int       level;
    int       inserting; //char pad2[4];
    pval_t    v;
    struct node_s *next[1];
} node_t;

typedef struct
{
    int    max_offset;
    int    max_level;
    int    nthreads;
    node_t *head;
    node_t *tail;
    char   pad[128];
} pq_t;

typedef ALIGNED(64) struct thread_data {
  val_t first;
  long range;
  int update;
  int unit_tx;
  int alternate;
  int randomized;
  int pq;
  int sl;
  int es;
  int effective;
  int first_remove;
  unsigned long nb_collisions;
  unsigned long nb_clean;
  unsigned long nb_add;
  unsigned long nb_added;
  unsigned long nb_remove;
  unsigned long nb_removed;
  unsigned long nb_contains;
  unsigned long nb_found;
  unsigned long nb_aborts;
  unsigned long nb_aborts_locked_read;
  unsigned long nb_aborts_locked_write;
  unsigned long nb_aborts_validate_read;
  unsigned long nb_aborts_validate_write;
  unsigned long nb_aborts_validate_commit;
  unsigned long nb_aborts_invalid_memory;
  unsigned long nb_aborts_double_write;
  unsigned long max_retries;
  unsigned int nb_threads;
  unsigned int seed;
  unsigned int seed2;
  sl_intset_t *set;
  barrier_t *barrier;
  unsigned long failures_because_contention;
  int id;

  /* LINDEN */
  int lin;
  pq_t *linden_set;
} thread_data_t;


#define get_marked_ref(_p)      ((void *)(((uintptr_t)(_p)) | 1))
#define get_unmarked_ref(_p)    ((void *)(((uintptr_t)(_p)) & ~1))
#define is_marked_ref(_p)       (((uintptr_t)(_p)) & 1)


/* Interface */

extern pq_t *pq_init(int max_offset);

extern void pq_destroy(pq_t *pq);

extern int insert(pq_t *pq, pkey_t k, pval_t v);

extern pval_t deletemin(pq_t *pq, thread_data_t *d);

extern void sequential_length(pq_t *pq);

extern int empty(pq_t *pq);

#endif // PRIOQ_H
