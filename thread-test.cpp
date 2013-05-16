#include <iostream>
#include <sstream>
#include <queue>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#define LIMIT_TOTAL 1000
#define LIMIT_SIMUL 200
#define NUM_THREADS 1
#define MESSAGE "Howdy"
#define PRODUCER_WAIT 10
#define CONSUMER_WAIT 120
#define WORKER_WAIT 75

using namespace std;

typedef struct {
  int id;
  char *message;
  void (* work)(int id);
} worker;

typedef struct {
  queue<worker *> *workers;
  pthread_mutex_t *mutex;
  pthread_cond_t *notFull, *notEmpty;
  bool producer_finished;
} worker_queue;

typedef struct {
  int id;
  worker_queue *wqueue;
} consumer_args;

void read_opts(int argc, char **argv);
void work(int id);
void execute_worker(worker *w);
void *producer(void *args);
void *consumer(void *args);
worker_queue *wqueue_init();
void worker_destroy(worker *w);
void wqueue_destroy(worker_queue *wqueue);
void log(int level, const char *format, ...);
void log_erase(int level, const char *format, ...);

int total, simul, num_threads, verbose, steps;

int main (int argc, char **argv)
{
  read_opts(argc, argv);

  worker_queue *wqueue;
  consumer_args *args[num_threads];
  pthread_t pro_thread, con_threads[num_threads];

  wqueue = wqueue_init();

  pthread_create(&pro_thread, NULL, producer, wqueue);
  log(0, "Created producer thread.\n");
  log(0, "Creating %d consumer threads...", num_threads);

  for (int i = 0; i < num_threads; i++) {
    args[i] = (consumer_args *) malloc(sizeof(consumer_args));
    args[i]->wqueue = wqueue;
    args[i]->id = i;

    pthread_create(&con_threads[i], NULL, consumer, args[i]);
  }

  pthread_join(pro_thread, NULL);

  for (int i = 0; i < num_threads; i++) {
    pthread_join(con_threads[i], NULL);
    free(args[i]);
  }

  wqueue_destroy(wqueue);

  return 0;
}

void read_opts(int argc, char **argv)
{
  int opt;

  total = LIMIT_TOTAL;
  simul = LIMIT_SIMUL;
  num_threads = NUM_THREADS;
  verbose = 0;
  steps = 1000;

  while ((opt = getopt(argc, argv, "w:q:t:v:s:")) != -1) {
    switch (opt) {
    case 'w':
      total = atoi(optarg);
      break;
    case 'q':
      simul = atoi(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    case 'v':
      verbose = atoi(optarg);
      break;
    case 's':
      steps = atoi(optarg);
      break;
    default:
      cout << "Usage: " << argv[0] << " [args]" << endl;
      cout << endl;
      cout << "Args:" << endl;
      cout << "-t consumer threads, default: 1" << endl;
      cout << "-w total workers, default: 1000" << endl;
      cout << "-q worker queue size, default: 200" << endl;
      cout << "-v verbosity: {0,1,2,3}, default: 0" << endl;
      cout << "-s reporting steps, default: 1000" << endl;
      cout << endl;

      exit(EXIT_FAILURE);
    }
  }
}

void work(int id)
{
  int wait = rand() % WORKER_WAIT;
  log(2, "Worker[%d]: Time spent = %d", id, wait);
  
  usleep(wait);
}

void execute_worker(worker *w)
{
  int divider = (int) ceilf((float) total / steps);
  int verbosity = ((w->id + 1) % divider) ? 1 : 0;
  float percent_complete = (float) (100 * (w->id + 1)) / total;
  int places = max(0, (int) log10f((float) steps) - 2);

  w->work(w->id);

  log_erase(
    verbosity, 
    "%s, worker %d/%d (%.*f%%) reporting for duty!", 
    w->message, 
    w->id + 1, 
    total, 
    places, 
    percent_complete
  );
}

void *producer(void *args)
{
  worker_queue *wqueue;
  worker *w;
  int wait;

  wqueue = (worker_queue *) args;

  log(1, "Producer: Starting.");

  for (int i = 0; i < total; i++) {
    w = (worker *) malloc(sizeof(worker));
    w->id = i;
    w->message = (char *) malloc(sizeof(MESSAGE));
    w->work = work;
    strcpy(w->message, MESSAGE);

    pthread_mutex_lock(wqueue->mutex);

    while (wqueue->workers->size() >= simul) {
      log(2, "Producer: waiting to add worker %d, queue FULL (%d slots filled).", w->id, wqueue->workers->size());
      pthread_cond_wait(wqueue->notFull, wqueue->mutex);
    }

    log(3, "Producer: Created worker %d.", w->id);
    wqueue->workers->push(w);

    log(
      2, 
      "Producer: Added worker %d (%s) to queue, qsize = %d (m = %d bytes)", 
      w->id, 
      w->message, 
      wqueue->workers->size(), 
      wqueue->workers->size() * sizeof(worker)
    );

    if (i + 1 == total) {
      log(1, "Producer: Finished, %d total jobs added", i + 1);
      wqueue->producer_finished = true;
    }

    pthread_mutex_unlock(wqueue->mutex);

    if (wqueue->producer_finished) {
      break;
    }

    pthread_cond_signal(wqueue->notEmpty);

    wait = rand() % PRODUCER_WAIT;
    log(3, "Producer wait: %d", wait);

    usleep(wait);
  }

  pthread_cond_broadcast(wqueue->notEmpty);
}

void *consumer(void *args)
{
  int id;
  worker_queue *wqueue;
  worker *w;
  int wait;
  int jobs_executed = 0;

  id = ((consumer_args *) args)->id;
  wqueue = ((consumer_args *) args)->wqueue;

  log(1, "Consumer[%d]: Starting.", id);

  while (true) {
    log(3, "Consumer[%d]: Checking queue.", id);
    pthread_mutex_lock(wqueue->mutex);

    while (wqueue->workers->empty() && !wqueue->producer_finished) {
      log(2, "Consumer[%d]: queue EMPTY.", id);
      pthread_cond_wait(wqueue->notEmpty, wqueue->mutex);
    }

    if (!wqueue->workers->empty()) {
      w = wqueue->workers->front();
      wqueue->workers->pop();

      log(
        2, 
        "Consumer[%d]: Removed worker %d (%s) from queue, qsize = %d (m = %d bytes)", 
        id,
        w->id, 
        w->message, 
        wqueue->workers->size(), 
        wqueue->workers->size() * sizeof(worker)
      );

      execute_worker(w);
      jobs_executed++;
      worker_destroy(w);
    }

    pthread_mutex_unlock(wqueue->mutex);

    if (wqueue->producer_finished && wqueue->workers->empty()) {
      break;
    }

    pthread_cond_signal(wqueue->notFull);
    
    wait = rand() % CONSUMER_WAIT;
    log(3, "Consumer[%d]: wait = %d", id, wait);
    
    usleep(wait);
  }

  log(1, "Consumer[%d]: Queue says Producer is finished, exiting after executing %d jobs.", id, jobs_executed);
  pthread_cond_signal(wqueue->notEmpty);
}

worker_queue *wqueue_init()
{
  worker_queue *wqueue = (worker_queue *) malloc(sizeof(worker_queue));

  wqueue->workers = new queue<worker *>();

  wqueue->mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init (wqueue->mutex, NULL);

  wqueue->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
  pthread_cond_init (wqueue->notFull, NULL);

  wqueue->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
  pthread_cond_init (wqueue->notEmpty, NULL);

  wqueue->producer_finished = false;

  return wqueue;
}

void wqueue_destroy(worker_queue *wqueue)
{
  delete wqueue->workers;
  free(wqueue->mutex);
  free(wqueue->notFull);
  free(wqueue->notEmpty);
  free(wqueue);
}


void worker_destroy(worker *w)
{
  free(w->message);
  free(w);
}

void log(int level, const char *format, ...)
{
  va_list argp;

  if (verbose >= level) {
    va_start(argp, format);
    fflush(stdout);
    vprintf(format, argp);
    printf("\n");
  }
}

void log_erase(int level, const char *format, ...)
{
  va_list argp;

  if (verbose >= level) {
    va_start(argp, format);
    printf("\33[2K\r");
    vprintf(format, argp);
    fflush(stdout);
  }
}
