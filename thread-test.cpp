#include <iostream>
#include <sstream>
#include <queue>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define LIMIT_TOTAL 1000
#define LIMIT_SIMUL 200
#define MESSAGE "Howdy"
#define PRODUCER_WAIT 10
#define CONSUMER_WAIT 120

using namespace std;

typedef struct {
  int id;
  char *message;
} worker;

typedef struct {
  queue<worker *> *workers;
  pthread_mutex_t *mutex;
  pthread_cond_t *notFull, *notEmpty;
} worker_queue;

void read_opts(int argc, char **argv);
void *producer(void *args);
void *consumer(void *args);
worker_queue *wqueue_init();
void worker_destroy(worker *w);
void wqueue_destroy(worker_queue *wqueue);
void log(int level, const char *format, ...);

int total, simul, verbose;

int main (int argc, char **argv)
{
  read_opts(argc, argv);

  worker_queue *wqueue = wqueue_init();
  pthread_t pro_thread, con_thread;

  pthread_create(&pro_thread, NULL, producer, wqueue);
  pthread_create(&con_thread, NULL, consumer, wqueue);
  pthread_join(pro_thread, NULL);
  pthread_join(con_thread, NULL);

  wqueue_destroy(wqueue);

  return 0;
}

void read_opts(int argc, char **argv)
{
  int opt;

  total = LIMIT_TOTAL;
  simul = LIMIT_SIMUL;
  verbose = 0;

  while ((opt = getopt(argc, argv, "w:q:v:")) != -1) {
    switch (opt) {
    case 'w':
      total = atoi(optarg);
      break;
    case 'q':
      simul = atoi(optarg);
      break;
    case 'v':
      verbose = atoi(optarg);
      break;
    default:
      cout << "Usage: " << argv[0] << " [-w <total workers> [-q <worker queue size> [-v<verbosity: {0,1,2,3}>]]]" 
           << endl;
      exit(EXIT_FAILURE);
    }
  }
}

void *producer(void *args)
{
  worker_queue *wqueue;
  worker *w;
  int wait;

  wqueue = (worker_queue *) args;

  for (int i = 0; i < total; i++) {
    w = (worker *) malloc(sizeof(worker));
    w->id = i;
    w->message = (char *) malloc(sizeof(MESSAGE));
    strcpy(w->message, MESSAGE);

    pthread_mutex_lock (wqueue->mutex);

    while (wqueue->workers->size() >= simul) {
      log(2, "Producer: waiting to add worker %d, queue FULL (%d slots filled).", w->id, wqueue->workers->size());
      pthread_cond_wait (wqueue->notFull, wqueue->mutex);
    }

    log(3, "Producer: Created worker %d.", w->id);
    wqueue->workers->push(w);

    log(
      1, 
      "Producer: Added worker %d (%s) to queue, qsize = %d (m = %d bytes)", 
      w->id, 
      w->message, 
      wqueue->workers->size(), 
      wqueue->workers->size() * sizeof(worker)
    );

    pthread_mutex_unlock (wqueue->mutex);
    pthread_cond_signal (wqueue->notEmpty);

    wait = rand() % PRODUCER_WAIT;
    log(3, "Producer wait: %d", wait);

    usleep (wait);
  }
}

void *consumer(void *args)
{
  worker_queue *wqueue;
  worker *w;
  int wait;

  wqueue = (worker_queue *) args;

  for (int i = 0; i < total; i++) {
    pthread_mutex_lock (wqueue->mutex);

    while (wqueue->workers->empty()) {
      log(2, "Consumer: queue EMPTY.");
      pthread_cond_wait (wqueue->notEmpty, wqueue->mutex);
    }

    w = wqueue->workers->front();
    wqueue->workers->pop();
    pthread_mutex_unlock (wqueue->mutex);
    pthread_cond_signal (wqueue->notFull);

    log(
      1, 
      "Consumer: Removed worker %d (%s) from queue, qsize = %d (m = %d bytes)", 
      w->id, 
      w->message, 
      wqueue->workers->size(), 
      wqueue->workers->size() * sizeof(worker)
    );

    worker_destroy(w);

    wait = rand() % CONSUMER_WAIT;
    log(3, "Consumer wait: %d", wait);

    usleep(wait);
  }
}

worker_queue *wqueue_init()
{
  worker_queue *wqueue = (worker_queue *) malloc(sizeof(worker_queue));

  wqueue->workers = new queue<worker *>();

  wqueue->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (wqueue->mutex, NULL);

  wqueue->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (wqueue->notFull, NULL);

  wqueue->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (wqueue->notEmpty, NULL);

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
