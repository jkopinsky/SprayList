/*
 * File:
 *   sssp.c
 * Author(s):
 *   Justin Kopinsky <jkopin@mit.edu>
 * Description:
 *   Single Source Shortest Path using concurrent priority queues
 *
 */

#include "intset.h"
#include "math.h"
#include "linden.h"
//#include "gc/ptst.h"

//#define STATIC
#define MAX_DEPS 10
#define MAX_NODES 9000000
#define MAX_DEG   4

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

typedef struct graph_node_s {
  int deg;
  slkey_t dist;
#ifdef STATIC
  int adj[MAX_DEG];
  int weights[MAX_DEG];
#else
  int *adj;
  int *weights;
#endif
  int times_processed;
} graph_node_t;

#ifdef STATIC
graph_node_t  nodes[MAX_NODES];
#else
graph_node_t *nodes;
#endif

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

void* sssp(void *data) {
  thread_data_t *d = (thread_data_t *)data;

  /* Create transaction */
  set_cpu(the_cores[d->id]);
  /* Wait on barrier */
  ssalloc_init();
  PF_CORRECTION;

  seeds = seed_rand();

#ifdef PIN
  int id = d->id;
  // int cpu = 40*(id/40) + 4*(id%10) + (id%40)/10;
  int cpu = 4*(id%20) + id/20; 
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

  // Begin SSSP

  int fail = 0;
  // int radius = 0;
  while (1) {
    val_t node;
    slkey_t dist_node;
  //   print_skiplist(d->set);
    while (1) { 
     if (d->sl) {
       if (spray_delete_min_key(d->set, &dist_node, &node, d)) break; // keep trying until get a node
     } else if (d->pq) {
       if (lotan_shavit_delete_min_key(d->set, &dist_node, &node, d)) break;
     } else if (d->lin) {
       node = (val_t) deletemin_key(d->linden_set, &dist_node, d); break;
     } else {
       printf("error: no queue selected\n");
       exit(1);
     }
     if (dist_node == -1) { // flag that list is empty
       break;
     }
     dist_node = 0;
    }
    if (dist_node == -1) { // list is empty; TODO make sure threads don't quit early
      fail++;
      if (fail > 20*d->nb_threads) { // TODO: really need a better break condition...
        break;
      }
      continue;
    }
    fail = 0;
    if (dist_node != nodes[node].dist) continue; // dead node
    nodes[node].times_processed++;

    int i;
    for (i = 0;i < nodes[node].deg;i++) {
      int v = nodes[node].adj[i];
      int w = nodes[node].weights[i];
      slkey_t dist_v = nodes[v].dist;
  //  printf("v=%d dist_v=%d\n", v, dist_v);
      if (dist_v == -1 || dist_node + w < dist_v) { // found better path to v
  //       printf("attempting cas...\n");
  //       printf("nodes[v].dist=%d dist_v=%d dist_node=%d\n", nodes[v].dist, dist_v, dist_node);
        int res = ATOMIC_CAS_MB(&nodes[v].dist, dist_v, dist_node+w);
  //       printf("%d nodes[%d].dist=%d\n", res, v, nodes[v].dist);
        if (res) {
          if (d->pq || d->sl) {
            sl_add_val(d->set, dist_node+w, v, TRANSACTIONAL); // add to queue only if CAS is successful
          } else if (d->lin) {
            insert(d->linden_set, dist_node+w, v);
          }
          d->nb_add++;
  //         if (dist_node+1 > radius) {
  //           radius = dist_node+1;
  //           printf("radius %d\n", radius);
  //         }
        } else {
          i--; // retry
        }
      } 
    }
  }

  // End SSSP
  
#ifdef PAPI
  if (PAPI_OK != PAPI_read_counters(g_values[d->id], G_EVENT_COUNT))
  {
    printf("Problem reading counters 2.");
  }
#endif

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
  pin(pthread_self(), 0);

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
    {"num-threads",               required_argument, NULL, 'n'},
    {"seed",                      required_argument, NULL, 's'},
    {"input-file",                required_argument, NULL, 'i'},
    {"output-file",               required_argument, NULL, 'o'},
    {"max-size",                  required_argument, NULL, 'm'},
    {"nothing",                   required_argument, NULL, 'l'},
    {NULL, 0, NULL, 0}
  };

  sl_intset_t *set;
  pq_t *linden_set;
  int i, c, size, edges;
  unsigned long reads, effreads, updates, collisions, effupds, 
                add, added, remove, removed;
  thread_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  struct timeval start, end;
  int nb_threads = DEFAULT_NB_THREADS;
  int seed = DEFAULT_SEED;
  int pq = DEFAULT_PQ;
  int sl = DEFAULT_SL;
  int lin = DEFAULT_LIN;
  char *input = "";
  char *output = "";
  int src = 0;
  int max = -1;
  int weighted = 0;
  int bimodal = 0;

  while(1) {
    i = 0;
    c = getopt_long(argc, argv, "hplLwbn:s:i:o:m:", long_options, &i);

    if(c == -1)
      break;

    if(c == 0 && long_options[i].flag == 0)
      c = long_options[i].val;

    switch(c) {
      case 0:
        break;
      case 'h':
        printf("SSSP "
            "(priority queue)\n"
            "\n"
            "Usage:\n"
            "  sssp [options...]\n"
            "\n"
            "Options:\n"
            "  -h, --help\n"
            "        Print this message\n"
            "  -l, --spray-list\n"
            "        Remove via delete_min operations using a spray list\n"
            "  -p, --priority-queue\n"
            "        Remove via delete_min operations using a skip list\n"
            "  -L, --linden\n"
            "        Use Linden's priority queue\n"
            "  -n, --num-threads <int>\n"
            "        Number of threads (default=" XSTR(DEFAULT_NB_THREADS) ")\n"
            "  -s, --seed <int>\n"
            "        RNG seed (0=time-based, default=" XSTR(DEFAULT_SEED) ")\n"
            "  -i, --input-file <string>\n"
            "        file to read the graph from (required) \n"
            "  -o, --output-file <string>\n"
            "        file to write the resulting shortest paths to\n"
            "  -m, --max-size <int>\n"
            "        if input graph exceeds max-size, use only first max-size nodes\n"
            "  -w, --weighted\n"
            "        use random edge weights uniformly chosen in [0,1]; fixed between trials given fixed seed\n"
            "  -b, --bimodal\n"
            "        use random edge weights chosen in [20,30]U[70,80]; fixed between trials given fixed seed\n"
            );
        exit(0);
      case 'l':
        sl = 1;
        break;
      case 'p':
        pq = 1;
        break;
      case 'L':
        lin = 1;
        break;
      case 'w':
        weighted = 1;
        break;
      case 'b':
        bimodal = 1;
        break;
      case 'n':
        nb_threads = atoi(optarg);
        break;
      case 's':
        seed = atoi(optarg);
        break;
      case 'i':
        input = optarg;
        break;
      case 'o':
        output = optarg;
        break;
      case 'm':
        max = atoi(optarg);
        break;
      case '?':
        printf("Use -h or --help for help\n");
        exit(0);
      default:
        exit(1);
    }
  }

  assert(nb_threads > 0);

  if (seed == 0)
    srand((int)time(0));
  else
    srand(seed);

  printf("Set type     : skip list\n");
  printf("Nb threads   : %d\n", nb_threads);
  printf("Seed         : %d\n", seed);
  printf("Priority Q   : %d\n", pq);
  printf("Spray List   : %d\n", sl);
  printf("Linden       : %d\n", lin);
  printf("Type sizes   : int=%d/long=%d/ptr=%d/word=%d\n",
      (int)sizeof(int),
      (int)sizeof(long),
      (int)sizeof(void *),
      (int)sizeof(uintptr_t));

  if ((data = (thread_data_t *)malloc(nb_threads * sizeof(thread_data_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  if ((threads = (pthread_t *)malloc(nb_threads * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  // TODO: Build graph here
  FILE* fp = fopen(input, "r");
  fscanf(fp, "# Nodes: %d Edges: %d\n", &size, &edges);
  if (size > max && max != -1) size = max;


#ifndef STATIC
  if ((nodes = (graph_node_t*)malloc(size * sizeof(graph_node_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
#endif
  
  for (i = 0;i < size;i++) {
    nodes[i].deg = 0;
    nodes[i].dist = -1;
    nodes[i].times_processed = 0;
  }

  int u,v;
  while (fscanf(fp, "%d %d\n", &u, &v) == 2) {
    if (u >= size) continue;
    if (v >= size) continue;
    nodes[u].deg++;
  }

#ifndef STATIC
  for (i = 0;i < size;i++) {
    if ((nodes[i].adj = (int*)malloc(nodes[i].deg * sizeof(int))) == NULL) {
      perror("malloc");
      exit(1);
    }
    if ((nodes[i].weights = (int*)malloc(nodes[i].deg * sizeof(int))) == NULL) {
      perror("malloc");
      exit(1);
    }
  }
#endif
  fclose(fp);
   
  nodes[src].dist = 0;


  fp = fopen(input, "r");
  int tmp;
  fscanf(fp, "# Nodes: %d Edges: %d\n", &tmp, &edges);

  int *idx;
  if ((idx= (int*)malloc(size * sizeof(int))) == NULL) {
    perror("malloc");
    exit(1);
  }
  for (i = 0;i < size;i++) {
    idx[i] = 0;
  }

  while (fscanf(fp, "%d %d\n", &u, &v) == 2) {
    if (u >= size) continue;
    if (v >= size) continue;
    assert(idx[u] < nodes[u].deg);
    nodes[u].adj[idx[u]] = v;
    if (weighted) {
      nodes[u].weights[idx[u]] = rand() % 100;
    } else if (bimodal) {
      if (rand() % 2) {
        nodes[u].weights[idx[u]] = (rand() % 11) + 20;
      } else {
        nodes[u].weights[idx[u]] = (rand() % 11) + 70;
      }
    } else {
      nodes[u].weights[idx[u]] = 1;
    }
    idx[u]++;
  }
  free(idx);

  // pq/sl
  *levelmax = floor_log_2(size)+2;
  set = sl_set_new();
  sl_add_val(set, 0, src, TRANSACTIONAL);

  // linden
  if (lin) {
    int offset = 32; // not sure what this does
     _init_gc_subsystem();
    linden_set = pq_init(offset);
    insert(linden_set, 1, src);
    nodes[src].dist = 1; // account for the fact that keys must be positive
  }


  printf("Graph size   : %d\n", size);
  printf("Level max    : %d\n", *levelmax);

  // Access set from all threads 
  barrier_init(&barrier, nb_threads + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  printf("Creating threads: ");
  for (i = 0; i < nb_threads; i++)
  {
    printf("%d, ", i);
    data[i].pq = pq;
    data[i].sl = sl;
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
    if (lin) {
      data[i].linden_set = linden_set;
    }

    if (pthread_create(&threads[i], &attr, sssp, (void *)(&data[i])) != 0) {
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



  /* stop = 0; */
  *running = 1;

  // Start threads 
  barrier_cross(&barrier);

  printf("STARTING...\n");
  gettimeofday(&start, NULL);

  // Wait for thread completion 
  for (i = 0; i < nb_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Error waiting for thread completion\n");
      exit(1);
    }
  }
  gettimeofday(&end, NULL);
  printf("STOPPING...\n");

  long nb_processed = 0;
  long unreachable = 0;
  if (strcmp(output,"")) {
    FILE *out = fopen(output, "w");
    for (i = 0;i < size;i++) {
      fprintf(out, "%d %lu\n", i, nodes[i].dist);
    }
    fclose(out);
  } else {
    for (i = 0;i < size;i++) {
      printf("%d %lu\n", i, nodes[i].dist);
    }
  }

  for (i = 0;i < size;i++) {
    nb_processed += nodes[i].times_processed;   
    if (nodes[i].times_processed == 0) {
      unreachable++;
    }
  }

  int duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
  printf ("duration = %d\n", duration);
  reads = 0;
  effreads = 0;
  updates = 0;
  collisions = 0;
  add = 0;
  added = 0;
  remove = 0;
  removed = 0;
  effupds = 0;
  for (i = 0; i < nb_threads; i++) {
    printf("Thread %d\n", i);
    printf("  #add        : %lu\n", data[i].nb_add);
    printf("    #added    : %lu\n", data[i].nb_added);
    printf("  #remove     : %lu\n", data[i].nb_remove);
    printf("    #removed  : %lu\n", data[i].nb_removed);
    printf("    #cleaned  : %lu\n", data[i].nb_clean);
    printf(" #collisions  : %lu\n", data[i].nb_collisions);
    printf("  #contains   : %lu\n", data[i].nb_contains);
    printf("  #found      : %lu\n", data[i].nb_found);
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
  }
  printf("Set size      : %d (expected: %d)\n", sl_set_size(set), size);
  printf("nodes processed:%lu\n", nb_processed);
  printf("unreachable   : %lu\n", unreachable);
  printf("wasted work   : %lu\n", nb_processed - (size - unreachable));

  printf("Duration      : %d (ms)\n", duration);
  printf("#ops          : %lu (%f / s)\n", reads + updates, (reads + updates) * 1000.0 / duration);

  printf("#read ops     : ");
  printf("%lu (%f / s)\n", reads, reads * 1000.0 / duration);

  printf("#eff. upd rate: %f \n", 100.0 * effupds / (effupds + effreads));

  printf("#update ops   : ");
  printf("%lu (%f / s)\n", updates, updates * 1000.0 / duration);

  printf("#total_remove : %lu\n", remove);
  printf("#total_removed: %lu\n", removed);
  printf("#total_add    : %lu\n", add);
  printf("#total_added  : %lu\n", added);
  printf("#net (rem-add): %lu\n", removed-added);
  printf("#total_collide: %lu\n", collisions);
  printf("#norm_collide : %f\n", ((double)collisions)/removed);

#ifdef PRINT_END
  print_skiplist(set);
#endif

#ifdef PAPI
  long total_L1_miss = 0;
  unsigned k = 0;
  for (k = 0; k < nb_threads; k++) {
    total_L1_miss += g_values[k][0];
    total_L2_miss += g_values[k][1];
    //printf("[Thread %d] L1_DCM: %lld\n", i, g_values[i][0]);
    //printf("[Thread %d] L2_DCM: %lld\n", i, g_values[i][1]);
  }
  printf("\n#L1 Cache Misses: %lld\n", total_L1_miss);
  printf("#Normalized Cache Misses: %f\n", ((double)total_L1_miss)/(reads+updates));
  printf("\n#L2 Cache Misses: %lld\n", total_L2_miss);
  printf("#Normalized Cache Misses: %f\n", ((double)total_L1_miss)/(reads+updates));
#endif

  // Delete set 
  if (pq || sl) {
    sl_set_delete(set);
  }

  free(threads);
  free(data);

  return 0;
}

