/*
 * File:
 *   test.c
 * Author(s):
 *   Vincent Gramoli <vincent.gramoli@epfl.ch>
 * Description:
 *   Concurrent accesses to skip list integer set
 *
 * Copyright (c) 2009-2010.
 *
 * test.c is part of Synchrobench
 * 
 * Synchrobench is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "intset.h"
#include "math.h"
#include "linden.h"
//#include "gc/ptst.h"

#define MAX_DEPS 10

//#define PAPI 1
#define PIN

#ifdef PIN
void
pin(pid_t t, int cpu) 
{
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);

  if( pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0 )
  {
   printf( "error setting affinity for %d %d\n", (int)t, (int)cpu );
  }
}
#endif

#ifdef PAPI
#include <papi.h>

#define G_EVENT_COUNT 2
#define MAX_THREADS 80
int g_events[] = { PAPI_L1_DCM, PAPI_L2_DCM };
long long g_values[MAX_THREADS][G_EVENT_COUNT] = {0,};
#endif

extern ALIGNED(64) uint8_t levelmax[64];
__thread unsigned long* seeds;

ALIGNED(64) uint8_t running[64];

val_t **deps; // for event simulator
int *nb_deps; // for event simulator

void barrier_init(barrier_t *b, int n)
{
  pthread_cond_init(&b->complete, NULL);
  pthread_mutex_init(&b->mutex, NULL);
  b->count = n;
  b->crossing = 0;
}

void barrier_cross(barrier_t *b)
{
  pthread_mutex_lock(&b->mutex);
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) {
    pthread_cond_wait(&b->complete, &b->mutex);
  } else {
    pthread_cond_broadcast(&b->complete);
    /* Reset for next time */
    b->crossing = 0;
  }
  pthread_mutex_unlock(&b->mutex);
}


void print_skiplist(sl_intset_t *set) {
  sl_node_t *curr;
  int i, j;
  int arr[*levelmax];

  for (i=0; i< sizeof arr/sizeof arr[0]; i++) arr[i] = 0;

  curr = set->head;
  do {
    printf("%d", (int) curr->val);
    for (i=0; i<curr->toplevel; i++) {
      printf("-*");
    }
    arr[curr->toplevel-1]++;
    printf("\n");
    curr = curr->next[0];
  } while (curr); 
  for (j=0; j<*levelmax; j++)
    printf("%d nodes of level %d\n", arr[j], j);
}


void* test(void *data) {
  int unext, last = -1; 
  val_t val = 0;
  pval_t pval = 0;

  thread_data_t *d = (thread_data_t *)data;

  /* Create transaction */
  TM_THREAD_ENTER(d->id);
  set_cpu(the_cores[d->id]);
  /* Wait on barrier */
  ssalloc_init();
  PF_CORRECTION;

  seeds = seed_rand();

#ifdef PIN
  int id = d->id;
  int cpu = 40*(id/40) + 4*(id%10) + (id%40)/10;
  // printf("Pinning %d to %d\n",id,cpu);
  pin(pthread_self(), cpu);
  //  pin(pthread_self(), id);
#endif

 #ifdef PAPI
    if (PAPI_OK != PAPI_start_counters(g_events, G_EVENT_COUNT))
  {
    printf("Problem starting counters 1.");
  }
 #endif


  barrier_cross(d->barrier);

  /* Is the first op an update? */
  unext = (rand_range_re(&d->seed, 100) - 1 < d->update);

#ifdef DISTRIBUTION_EXPERIMENT
  while (1)
#else
  while (*running)
#endif
    {		
      if (d->es) { // event simulator experiment
        if (d->lin) {
          if (!empty(d->linden_set)) {
            d->nb_remove++;
            pval_t pval = deletemin(d->linden_set, d);
            d->nb_removed++;

  //           printf("%d %d\n", pval, deps[pval][0]);

            int i = 0;
            val_t dep;
            while ((dep = deps[pval][i]) != -1 && i < MAX_DEPS) {
              d->nb_add++;
              if (insert(d->linden_set, dep, dep)) {
                d->nb_added++;
              }
              i++;
            }
          }
        } else {
          if (d->set->head->next[0]->next[0] != NULL) {// set not empty
            d->nb_remove++;
            if (d->sl) { // spray list
              if (spray_delete_min(d->set, &val, d)) {
                d->nb_removed++;
              } else {
                continue;
              }
            } else if (d->pq) { // naive pq
              if (naive_delete_min(d->set, &val, d)) {
                d->nb_removed++;
                //         continue; // TODO: maybe try remove this to simulate task handling (dependency checks still occur)
              } else {
                continue;
              }
            }

            //         struct timespec ten_usec;
            //         ten_usec.tv_sec = 0;
            //         ten_usec.tv_nsec = 10000;
            //         nanosleep(&ten_usec, NULL);

            // dependency handling
            int i = 0;
            val_t dep;
            while ((dep = deps[val][i]) != -1 && i < MAX_DEPS) {
              if (!sl_contains(d->set, dep, TRANSACTIONAL)) { // dependent has been removed, need to add it again
                if (sl_add(d->set, dep, TRANSACTIONAL)) { // check if insert actually succeeded (otherwise someone else did it first)
                  d->nb_added++;
                }
                d->nb_add++;
              }
              i++;
            }
          }
        }
      } else { // not event simulator
        if (unext) { // update

          if (last < 0) { // add
            val = rand_range_re(&d->seed, d->range);
            if (d->lin) {
              pval = val;
              insert(d->linden_set, pval, pval);
              d->nb_added++;
              last = pval;
            } else { // not linden
              if (sl_add(d->set, val, TRANSACTIONAL)) {
                d->nb_added++;
                last = val;
              } 				
            }
            d->nb_add++;

          } else { // remove

            if (d->pq) {
              if (naive_delete_min(d->set, &val, d)) {
                d->nb_removed++;
                if (d->first_remove == -1) {
                  d->first_remove = val;
                }
              }
                last = -1;
            }
            else if (d->sl) {
              if (spray_delete_min(d->set, &val, d)) {
                d->nb_removed++;
                if (d->first_remove == -1) {
                  d->first_remove = val;
                }
                last = -1;
              }
            }
            else if (d->lin) {
              if ((pval = deletemin(d->linden_set, d))) {
                d->nb_removed++;
                if (d->first_remove == -1) {
                  d->first_remove = pval;
                }
                last = -1;
              }
            }
            else if (d->alternate) { // alternate mode (default)
              if (sl_remove(d->set, last, TRANSACTIONAL)) {
                d->nb_removed++;
                if (d->first_remove == -1) {
                  d->first_remove = val;
                }
              } 
              last = -1;
            } else {
              /* Random computation only in non-alternated cases */
              val = rand_range_re(&d->seed, d->range);
              /* Remove one random value */
              if (sl_remove_succ(d->set, val, TRANSACTIONAL)) {
                d->nb_removed++;
                if (d->first_remove == -1) {
                  d->first_remove = val;
                }
                /* Repeat until successful, to avoid size variations */
                last = -1;
              } 
            }
            d->nb_remove++;
          }

        } else { // read

          if (d->alternate) {
            if (d->update == 0) {
              if (last < 0) {
                val = d->first;
                last = val;
              } else { // last >= 0
                val = rand_range_re(&d->seed, d->range);
                last = -1;
              }
            } else { // update != 0
              if (last < 0) {
                val = rand_range_re(&d->seed, d->range);
                //last = val;
              } else {
                val = last;
              }
            }
          }	else val = rand_range_re(&d->seed, d->range);

          PF_START(2);
          if (sl_contains(d->set, val, TRANSACTIONAL)) 
            d->nb_found++;
          PF_STOP(2);	
          d->nb_contains++;
        }

        /* Is the next op an update? */
        if (d->effective) { // a failed remove/add is a read-only tx
          unext = ((100 * (d->nb_added + d->nb_removed))
              < (d->update * (d->nb_add + d->nb_remove + d->nb_contains)));
        } else { // remove/add (even failed) is considered as an update
          unext = (rand_range_re(&d->seed, 100) - 1 < d->update);
        }
      }

#ifdef DISTRIBUTION_EXPERIMENT
      if (d->first_remove != -1) {
        break; //only one run
      }
#endif

    }
#ifdef PAPI
  if (PAPI_OK != PAPI_read_counters(g_values[d->id], G_EVENT_COUNT))
  {
    printf("Problem reading counters 2.");
  }
#endif

  /* Free transaction */
  TM_THREAD_EXIT();

  PF_PRINT;

  return NULL;
}

void catcher(int sig)
{
  printf("CAUGHT SIGNAL %d\n", sig);
}

int main(int argc, char **argv)
{
  set_cpu(the_cores[0]);
  ssalloc_init();
  seeds = seed_rand();

#ifdef PAPI
  if (PAPI_VER_CURRENT != PAPI_library_init(PAPI_VER_CURRENT))
  {
    printf("PAPI_library_init error.\n");
    return 0; 
  }
  else 
  {
    printf("PAPI_library_init success.\n");
  }

  if (PAPI_OK != PAPI_query_event(PAPI_L1_DCM))
  {
    printf("Cannot count PAPI_L1_DCM.");
  }
  printf("PAPI_query_event: PAPI_L1_DCM OK.\n");
  if (PAPI_OK != PAPI_query_event(PAPI_L2_DCM))
  {
    printf("Cannot count PAPI_L2_DCM.");
  }
  printf("PAPI_query_event: PAPI_L2_DCM OK.\n");

#endif

  struct option long_options[] = {
    // These options don't set a flag
    {"help",                      no_argument,       NULL, 'h'},
    {"duration",                  required_argument, NULL, 'd'},
    {"priority-queue",            required_argument, NULL, 'p'},
    {"linden",                    required_argument, NULL, 'L'},
    {"spray-list",                required_argument, NULL, 'l'},
    {"event-simulator",           required_argument, NULL, 'e'},
    {"initial-size",              required_argument, NULL, 'i'},
    {"num-threads",               required_argument, NULL, 'n'},
    {"range",                     required_argument, NULL, 'r'},
    {"seed",                      required_argument, NULL, 's'},
    {"update-rate",               required_argument, NULL, 'u'},
    {"elasticity",                required_argument, NULL, 'x'},
    {"nothing",                   required_argument, NULL, 'l'},
    {NULL, 0, NULL, 0}
  };

  sl_intset_t *set;
  pq_t *linden_set;
  int i, c, size;
  val_t last = 0; 
  val_t val = 0;
  pval_t pval = 0;
  unsigned long reads, effreads, updates, collisions, effupds, aborts, aborts_locked_read, aborts_locked_write,
                aborts_validate_read, aborts_validate_write, aborts_validate_commit, add, added, remove, removed,
                aborts_invalid_memory, aborts_double_write, max_retries, failures_because_contention, depdist;
  thread_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  struct timeval start, end;
  struct timespec timeout;
  int duration = DEFAULT_DURATION;
  int initial = DEFAULT_INITIAL;
  int nb_threads = DEFAULT_NB_THREADS;
  long range = DEFAULT_RANGE;
  int seed = DEFAULT_SEED;
  int seed2 = DEFAULT_SEED;
  int update = DEFAULT_UPDATE;
  int unit_tx = DEFAULT_ELASTICITY;
  int alternate = DEFAULT_ALTERNATE;
  int pq = DEFAULT_PQ;
  int sl = DEFAULT_SL;
  int es = DEFAULT_ES;
  int lin = DEFAULT_LIN;
  int effective = DEFAULT_EFFECTIVE;
  sigset_t block_set;

  while(1) {
    i = 0;
    c = getopt_long(argc, argv, "hAplLe:f:d:i:n:r:s:u:x:l:", long_options, &i);

    if(c == -1)
      break;

    if(c == 0 && long_options[i].flag == 0)
      c = long_options[i].val;

    switch(c) {
      case 0:
        break;
      case 'h':
        printf("intset -- STM stress test "
            "(skip list)\n"
            "\n"
            "Usage:\n"
            "  intset [options...]\n"
            "\n"
            "Options:\n"
            "  -h, --help\n"
            "        Print this message\n"
            "  -A, --Alternate\n"
            "        Consecutive insert/remove target the same value\n"
            "  -l, --spray-list\n"
            "        Remove via delete_min operations using a spray list\n"
            "  -p, --priority-queue\n"
            "        Remove via delete_min operations using a Lotan-Shavit queue\n"
            "  -e, --event-simulator\n"
            "        Descrete event simulator experiment, parameter = dependency distance\n"
            "  -L, --linden\n"
            "        Use Linden's priority queue\n"
            "  -f, --effective <int>\n"
            "        update txs must effectively write (0=trial, 1=effective, default=" XSTR(DEFAULT_EFFECTIVE) ")\n"
            "  -d, --duration <int>\n"
            "        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
            "  -i, --initial-size <int>\n"
            "        Number of elements to insert before test (default=" XSTR(DEFAULT_INITIAL) ")\n"
            "  -n, --num-threads <int>\n"
            "        Number of threads (default=" XSTR(DEFAULT_NB_THREADS) ")\n"
            "  -r, --range <int>\n"
            "        Range of integer values inserted in set (default=" XSTR(DEFAULT_RANGE) ")\n"
            "  -s, --seed <int>\n"
            "        RNG seed (0=time-based, default=" XSTR(DEFAULT_SEED) ")\n"
            "  -u, --update-rate <int>\n"
            "        Percentage of update transactions (default=" XSTR(DEFAULT_UPDATE) ")\n"
            "  -x, --elasticity (default=4)\n"
            "        Use elastic transactions\n"
            "        0 = non-protected,\n"
            "        1 = normal transaction,\n"
            "        2 = read elastic-tx,\n"
            "        3 = read/add elastic-tx,\n"
            "        4 = read/add/rem elastic-tx,\n"
            "        5 = fraser lock-free\n"
            );
        exit(0);
      case 'A':
        alternate = 1;
        break;
      case 'l':
        sl = 1;
        break;
      case 'p':
        pq = 1;
        break;
      case 'e':
        es = 1;
        depdist = atoi(optarg);
        break;
      case 'L':
        lin = 1;
        break;
      case 'f':
        effective = atoi(optarg);
        break;
      case 'd':
        duration = atoi(optarg);
        break;
      case 'i':
        initial = atoi(optarg);
        break;
      case 'n':
        nb_threads = atoi(optarg);
        break;
      case 'r':
        range = atol(optarg);
        break;
      case 's':
        seed = atoi(optarg);
        break;
      case 'u':
        update = atoi(optarg);
        break;
      case 'x':
        unit_tx = atoi(optarg);
        break;
      case '?':
        printf("Use -h or --help for help\n");
        exit(0);
      default:
        exit(1);
    }
  }

  assert(duration >= 0);
  assert(initial >= 0);
  assert(nb_threads > 0);
  assert(range > 0);
  assert(update >= 0 && update <= 100);

  // if (range < initial)
  // {
  range = 100000000;
  // }

  printf("Set type     : skip list\n");
  printf("Duration     : %d\n", duration);
  printf("Initial size : %u\n", initial);
  printf("Nb threads   : %d\n", nb_threads);
  printf("Value range  : %ld\n", range);
  printf("Seed         : %d\n", seed);
  printf("Update rate  : %d\n", update);
  printf("Elasticity   : %d\n", unit_tx);
  printf("Alternate    : %d\n", alternate);
  printf("Priority Q   : %d\n", pq);
  printf("Spray List   : %d\n", sl);
  printf("Linden       : %d\n", lin);
  printf("Efffective   : %d\n", effective);
  printf("Type sizes   : int=%d/long=%d/ptr=%d/word=%d\n",
      (int)sizeof(int),
      (int)sizeof(long),
      (int)sizeof(void *),
      (int)sizeof(uintptr_t));

  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

  if ((data = (thread_data_t *)malloc(nb_threads * sizeof(thread_data_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  if ((threads = (pthread_t *)malloc(nb_threads * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  if (seed == 0)
    srand((int)time(0));
  else
    srand(seed);

  *levelmax = floor_log_2((unsigned int) initial);
  set = sl_set_new();

  /* stop = 0; */
  *running = 1;

  // Init STM 
  printf("Initializing STM\n");

  TM_STARTUP();

  // Populate set 
  printf("Adding %d entries to set\n", initial);
  i = 0;

  if (lin) {
    int offset = 32; // not sure what this does
     _init_gc_subsystem();
    linden_set = pq_init(offset);
  }

  if (es) { // event simulator has event ids 1..m
    // no timeout in ES, finishes when list is empty
    //   timeout.tv_sec = 0;
    //   timeout.tv_nsec = 0;

    if ((nb_deps = (int *)malloc(initial * sizeof(int))) == NULL) {
      perror("malloc");
      exit(1);
    }
    if ((deps = (val_t **)malloc(initial * sizeof(val_t*))) == NULL) {
      perror("malloc");
      exit(1);
    }
    while (i < initial)
    {
      if ((deps[i] = (val_t*)malloc(MAX_DEPS * sizeof(val_t))) == NULL) {
        perror("malloc");
        exit(1);
      }

      int num_deps = 0;
      nb_deps[i] = 0;

      if (lin) {
        insert(linden_set, i, i);
      } else {
        sl_add(set, (val_t)i, 0);
      }

      while (i < initial-1 && num_deps < MAX_DEPS &&
          rand_range_re(NULL, 3) % 2) { // Add geometrically distributed # of deps TODO: parametrize '2'
        val_t dep = ((val_t)i)+1;
        int dep_var = sqrt(depdist);
        dep += depdist + rand_range_re(NULL,2*dep_var) - dep_var;
        if (dep >= initial) dep = initial-1;
        //       while (dep < initial-1 && rand_range_re(NULL, 11) % 10) { // dep should be i+GEO(10) TODO: parametrize '10'
        //         dep++;
        //       }
        deps[i][num_deps] = dep;
        num_deps++;
      }
      nb_deps[i] = num_deps;
      if (num_deps < MAX_DEPS) {
        deps[i][num_deps] = -1; // marks last dep
      }

      i++;
    }
  } else if (lin) {
    while (i < initial)
    {
#ifdef DISTRIBUTION_EXPERIMENT
      pval = i;
#else
      pval = rand_range_re(NULL, range);
#endif
      insert(linden_set, pval, pval);
      last = pval;
      i++;
    }
  } else {
    while (i < initial)
    {
#ifdef DISTRIBUTION_EXPERIMENT
      val = i;
#else
      val = rand_range_re(NULL, range);
#endif
      if (sl_add(set, val, 0))
      {
        last = val;
        i++;
      }
    }
  }

#ifdef PRINT_LIST
  print_skiplist(set);
#endif


  size = sl_set_size(set);
  printf("Set size     : %d\n", size);
  printf("Level max    : %d\n", *levelmax);

  // Access set from all threads 
  barrier_init(&barrier, nb_threads + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  printf("Creating threads: ");
  for (i = 0; i < nb_threads; i++)
  {
    printf("%d, ", i);
    data[i].first = last;
    data[i].range = range;
    data[i].update = update;
    data[i].unit_tx = unit_tx;
    data[i].alternate = alternate;
    data[i].pq = pq;
    data[i].sl = sl;
    data[i].es = es;
    data[i].effective = effective;
    data[i].first_remove = -1;
    data[i].nb_collisions = 0;
    data[i].nb_add = 0;
    data[i].nb_clean = 0;
    data[i].nb_added = 0;
    data[i].nb_remove = 0;
    data[i].nb_removed = 0;
    data[i].nb_contains = 0;
    data[i].nb_found = 0;
    data[i].nb_aborts = 0;
    data[i].nb_aborts_locked_read = 0;
    data[i].nb_aborts_locked_write = 0;
    data[i].nb_aborts_validate_read = 0;
    data[i].nb_aborts_validate_write = 0;
    data[i].nb_aborts_validate_commit = 0;
    data[i].nb_aborts_invalid_memory = 0;
    data[i].nb_aborts_double_write = 0;
    data[i].max_retries = 0;
    data[i].nb_threads = nb_threads;
    data[i].seed = rand();
    data[i].seed2 = rand();
    data[i].set = set;
    data[i].barrier = &barrier;
    data[i].failures_because_contention = 0;
    data[i].id = i;

    /* LINDEN */
    data[i].lin = lin;
    data[i].linden_set = linden_set;

    if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0) {
      fprintf(stderr, "Error creating thread\n");
      exit(1);
    }
  }
  pthread_attr_destroy(&attr);

  // Catch some signals 
  if (signal(SIGHUP, catcher) == SIG_ERR ||
      //signal(SIGINT, catcher) == SIG_ERR ||
      signal(SIGTERM, catcher) == SIG_ERR) {
    perror("signal");
    exit(1);
  }

  // Start threads 
  barrier_cross(&barrier);

  printf("STARTING...\n");
  gettimeofday(&start, NULL);

#ifndef DISTRIBUTION_EXPERIMENT // don't sleep if doing distro experiment
  if (duration > 0) {
    nanosleep(&timeout, NULL);
  } else {
    sigemptyset(&block_set);
    sigsuspend(&block_set);
  }
#endif

  /* AO_store_full(&stop, 1); */
  *running = 0;

  // if (!es) {
  gettimeofday(&end, NULL);
  // }
  printf("STOPPING...\n");

  // Wait for thread completion 
  for (i = 0; i < nb_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Error waiting for thread completion\n");
      exit(1);
    }
  }

  // if (es) {
  //   gettimeofday(&end, NULL); // time when all threads finish
  // }

  printf ("duration = %d\n", duration);
  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
  printf ("duration = %d\n", duration);
  aborts = 0;
  aborts_locked_read = 0;
  aborts_locked_write = 0;
  aborts_validate_read = 0;
  aborts_validate_write = 0;
  aborts_validate_commit = 0;
  aborts_invalid_memory = 0;
  aborts_double_write = 0;
  failures_because_contention = 0;
  reads = 0;
  effreads = 0;
  updates = 0;
  collisions = 0;
  add = 0;
  added = 0;
  remove = 0;
  removed = 0;
  effupds = 0;
  max_retries = 0;
  for (i = 0; i < nb_threads; i++) {
    printf("Thread %d\n", i);
    printf("  #add        : %lu\n", data[i].nb_add);
    printf("    #added    : %lu\n", data[i].nb_added);
    printf("  #remove     : %lu\n", data[i].nb_remove);
    printf("    #removed  : %lu\n", data[i].nb_removed);
    printf("    #cleaned  : %lu\n", data[i].nb_clean);
    printf("first remove  : %d\n", data[i].first_remove);
    printf(" #collisions  : %lu\n", data[i].nb_collisions);
    printf("  #contains   : %lu\n", data[i].nb_contains);
    printf("  #found      : %lu\n", data[i].nb_found);
    printf("  #aborts     : %lu\n", data[i].nb_aborts);
    printf("    #lock-r   : %lu\n", data[i].nb_aborts_locked_read);
    printf("    #lock-w   : %lu\n", data[i].nb_aborts_locked_write);
    printf("    #val-r    : %lu\n", data[i].nb_aborts_validate_read);
    printf("    #val-w    : %lu\n", data[i].nb_aborts_validate_write);
    printf("    #val-c    : %lu\n", data[i].nb_aborts_validate_commit);
    printf("    #inv-mem  : %lu\n", data[i].nb_aborts_invalid_memory);
    printf("    #dup-w    : %lu\n", data[i].nb_aborts_double_write);
    printf("    #failures : %lu\n", data[i].failures_because_contention);
    printf("  Max retries : %lu\n", data[i].max_retries);
    aborts += data[i].nb_aborts;
    aborts_locked_read += data[i].nb_aborts_locked_read;
    aborts_locked_write += data[i].nb_aborts_locked_write;
    aborts_validate_read += data[i].nb_aborts_validate_read;
    aborts_validate_write += data[i].nb_aborts_validate_write;
    aborts_validate_commit += data[i].nb_aborts_validate_commit;
    aborts_invalid_memory += data[i].nb_aborts_invalid_memory;
    aborts_double_write += data[i].nb_aborts_double_write;
    failures_because_contention += data[i].failures_because_contention;
    reads += data[i].nb_contains;
    effreads += data[i].nb_contains + 
      (data[i].nb_add - data[i].nb_added) + 
      (data[i].nb_remove - data[i].nb_removed); 
    updates += (data[i].nb_add + data[i].nb_remove);
    collisions += data[i].nb_collisions;
    add += data[i].nb_add;
    added += data[i].nb_added;
    remove += data[i].nb_remove;
    removed += data[i].nb_removed;
    effupds += data[i].nb_removed + data[i].nb_added; 
    size += data[i].nb_added - data[i].nb_removed;
    if (max_retries < data[i].max_retries)
      max_retries = data[i].max_retries;
  }
  printf("Set size      : %d (expected: %d)\n", sl_set_size(set), size);
  printf("Duration      : %d (ms)\n", duration);
  printf("#txs          : %lu (%f / s)\n", reads + updates, (reads + updates) * 1000.0 / duration);

  printf("#read txs     : ");
  if (effective) {
    printf("%lu (%f / s)\n", effreads, effreads * 1000.0 / duration);
    printf("  #contains   : %lu (%f / s)\n", reads, reads * 1000.0 / duration);
  } else printf("%lu (%f / s)\n", reads, reads * 1000.0 / duration);

  printf("#eff. upd rate: %f \n", 100.0 * effupds / (effupds + effreads));

  printf("#update txs   : ");
  if (effective) {
    printf("%lu (%f / s)\n", effupds, effupds * 1000.0 / duration);
    printf("  #upd trials : %lu (%f / s)\n", updates, updates * 1000.0 / 
        duration);
  } else printf("%lu (%f / s)\n", updates, updates * 1000.0 / duration);

  printf("#total_remove : %lu\n", remove);
  printf("#total_removed: %lu\n", removed);
  printf("#total_add    : %lu\n", add);
  printf("#total_added  : %lu\n", added);
  if (data[0].es) {
    printf("#net (rem-add): %lu\n", removed-added);
  }
  printf("#total_collide: %lu\n", collisions);
  printf("#norm_collide : %f\n", ((double)collisions)/removed);


  printf("#aborts       : %lu (%f / s)\n", aborts, aborts * 1000.0 / duration);
  printf("  #lock-r     : %lu (%f / s)\n", aborts_locked_read, aborts_locked_read * 1000.0 / duration);
  printf("  #lock-w     : %lu (%f / s)\n", aborts_locked_write, aborts_locked_write * 1000.0 / duration);
  printf("  #val-r      : %lu (%f / s)\n", aborts_validate_read, aborts_validate_read * 1000.0 / duration);
  printf("  #val-w      : %lu (%f / s)\n", aborts_validate_write, aborts_validate_write * 1000.0 / duration);
  printf("  #val-c      : %lu (%f / s)\n", aborts_validate_commit, aborts_validate_commit * 1000.0 / duration);
  printf("  #inv-mem    : %lu (%f / s)\n", aborts_invalid_memory, aborts_invalid_memory * 1000.0 / duration);
  printf("  #dup-w      : %lu (%f / s)\n", aborts_double_write, aborts_double_write * 1000.0 / duration);
  printf("  #failures   : %lu\n",  failures_because_contention);
  printf("Max retries   : %lu\n", max_retries);

#ifdef PRINT_END
  print_skiplist(set);
#endif

#ifdef PAPI
  long total_L1_miss = 0;
  unsigned k = 0;
  for (k = 0; k < nb_threads; k++) {
    total_L1_miss += g_values[k][0];
    //printf("[Thread %d] L1_DCM: %lld\n", i, g_values[i][0]);
    //printf("[Thread %d] L2_DCM: %lld\n", i, g_values[i][1]);
  }
  printf("\n#L1 Cache Misses: %lld\n", total_L1_miss);
  printf("#Normalized Cache Misses: %f\n", ((double)total_L1_miss)/(reads+updates));
#endif

  // Delete set 
  sl_set_delete(set);

  // Cleanup STM 
  TM_SHUTDOWN();

  free(threads);
  free(data);

  return 0;
}

