#include <iostream>
#include <sstream>
#include <queue>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// #define LIMIT_TOTAL 1000
// #define LIMIT_SIMUL 200
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

void *producer(void *args);
void *consumer(void *args);
worker_queue *wqueue_init();
void worker_destroy(worker *w);
void wqueue_destroy(worker_queue *wqueue);
void log(int level, string message);

int total, simul, verbose;

int main (int argc, char **argv)
{
  if (argc < 4) {
    cout << "Usage: " << argv[0] << " <total workers> <worker queue size> <verbose: {0,1,2,3}>" << endl;
  } else {
    total = strtol(argv[1], (char **) NULL, 10);
    simul = strtol(argv[2], (char **) NULL, 10);
    verbose = strtol(argv[3], (char **) NULL, 10);
  }

  worker_queue *wqueue = wqueue_init();
  pthread_t pro_thread, con_thread;

  pthread_create(&pro_thread, NULL, producer, wqueue);
  pthread_create(&con_thread, NULL, consumer, wqueue);
  pthread_join(pro_thread, NULL);
  pthread_join(con_thread, NULL);

  wqueue_destroy(wqueue);

  return 0;
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
      stringstream l_message;
      l_message << "Producer: waiting to add worker " << w->id << ", queue FULL (" << wqueue->workers->size()
                << " slots filled).";

      log(2, l_message.str());

      pthread_cond_wait (wqueue->notFull, wqueue->mutex);
    }

    // cout << "Producer: Created worker " << w->id << "." << endl;
    wqueue->workers->push(w);

    if (verbose > 0) {
      cout << "Producer: Added worker " << w->id << " (" << w->message << ") to queue, qsize = "
           << wqueue->workers->size() << " (m = " << wqueue->workers->size() * sizeof(worker) << " bytes)" << endl;
    }

    pthread_mutex_unlock (wqueue->mutex);
    pthread_cond_signal (wqueue->notEmpty);

    wait = rand() % PRODUCER_WAIT;
    // cout << "Producer wait: " << wait << endl;

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
      // cout << "Consumer: queue EMPTY." << endl;
      pthread_cond_wait (wqueue->notEmpty, wqueue->mutex);
    }

    w = wqueue->workers->front();
    wqueue->workers->pop();
    pthread_mutex_unlock (wqueue->mutex);
    pthread_cond_signal (wqueue->notFull);

    cout << "Consumer: Removed worker " << w->id << " (" << w->message << ") from queue, qsize = "
         << wqueue->workers->size() << " (m = " << wqueue->workers->size() * sizeof(worker) << " bytes)" << endl;

    worker_destroy(w);
    wait = rand() % CONSUMER_WAIT;
    // cout << "Consumer wait: " << wait << endl;

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

void log(int level, string message)
{
  if (verbose >= level) {
    cout << message <<endl;
  }
}
