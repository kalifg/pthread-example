#include <iostream>
#include <sstream>
#include <queue>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <limits.h>
#include <unistd.h>

#define TOTAL_JOBS 1000
#define QUEUE_SIZE 200
#define NUM_THREADS 1
#define MESSAGE "Howdy"
#define PRODUCER_WAIT 10
#define WORKER_WAIT 120
#define JOB_WAIT 75

using namespace std;

typedef struct {
  int id;
  char *message;
  void (* work)(int id);
} job;

typedef struct {
  queue<job *> *jobs;
  pthread_mutex_t *mutex;
  pthread_cond_t *notFull, *notEmpty;
  bool producer_finished;
} job_queue;

typedef struct {
  int id;
  job_queue *jqueue;
} worker_args;

void read_opts(int argc, char **argv);
void work(int id);
void execute_job(job *j);
void *producer(void *args);
void *worker(void *args);
job_queue *jqueue_init();
void job_destroy(job *j);
void jqueue_destroy(job_queue *jqueue);
void log(int level, const char *format, ...);
void log_erase(int level, const char *format, ...);

int total_jobs, simul, num_threads, verbose, steps;

int main (int argc, char **argv)
{
  read_opts(argc, argv);

  job_queue *jqueue;
  worker_args *args[num_threads];
  pthread_t pro_thread, con_threads[num_threads];
  int create_flag;
  pthread_attr_t pattr;

  // Adjusting this from the default greatly reduces the amount of memory used 
  // per thread.  Too small however, and your thread won't be created or your 
  // jobs won't run.
  // pthread_attr_setstacksize(&pattr, PTHREAD_STACK_MIN);
  
  jqueue = jqueue_init();

  pthread_create(&pro_thread, NULL, producer, jqueue);
  log(0, "Created producer thread.\n");
  log(0, "Creating %d worker threads...", num_threads);

  for (int i = 0; i < num_threads; i++) {
    args[i] = (worker_args *) malloc(sizeof(worker_args));
    args[i]->jqueue = jqueue;
    args[i]->id = i;

    // create_flag = pthread_create(&con_threads[i], &pattr, worker, args[i]);
    create_flag = pthread_create(&con_threads[i], NULL, worker, args[i]);

    if (create_flag) {
      switch (create_flag) {
      case EAGAIN:
        cout << "System thread limit reached (" << i << ").";
        break;
      default:
        cout << "Error in thread attributes.";
      }

      cout << "  No more threads will be created." << endl;
      num_threads = i;
    }
  }

  if (num_threads > 0) {
    pthread_join(pro_thread, NULL);

    for (int i = 0; i < num_threads; i++) {
      pthread_join(con_threads[i], NULL);
      free(args[i]);
    }

    jqueue_destroy(jqueue);
  }

  cout << endl;
  return 0;
}

void read_opts(int argc, char **argv)
{
  int opt;

  total_jobs = TOTAL_JOBS;
  simul = QUEUE_SIZE;
  num_threads = NUM_THREADS;
  verbose = 0;
  steps = 1000;

  while ((opt = getopt(argc, argv, "j:q:t:v:s:")) != -1) {
    switch (opt) {
    case 'j':
      total_jobs = atoi(optarg);
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
      cout << "-t worker threads, default: 1" << endl;
      cout << "-j total jobs, default: 1000" << endl;
      cout << "-q job queue size, default: 200" << endl;
      cout << "-v verbosity: {0,1,2,3}, default: 0" << endl;
      cout << "-s reporting steps, default: 1000" << endl;
      cout << endl;

      exit(EXIT_FAILURE);
    }
  }
}

void work(int id)
{
  int wait = rand() % JOB_WAIT;
  log(2, "Job[%d]: Time spent = %d", id, wait);
  
  usleep(wait);
}

void execute_job(job *j)
{
  int divider = (int) ceilf((float) total_jobs / steps);
  int verbosity = ((j->id + 1) % divider) ? 1 : 0;
  float percent_complete = (float) (100 * (j->id + 1)) / total_jobs;
  int places = max(0, (int) log10f((float) steps) - 2);

  j->work(j->id);

  log_erase(
    verbosity, 
    "%s, job %d/%d (%.*f%%) reporting for duty!", 
    j->message, 
    j->id + 1, 
    total_jobs, 
    places, 
    percent_complete
  );
}

void *producer(void *args)
{
  job_queue *jqueue;
  job *j;
  int wait;

  jqueue = (job_queue *) args;

  log(1, "Producer: Starting.");

  for (int i = 0; i < total_jobs; i++) {
    j = (job *) malloc(sizeof(job));
    j->id = i;
    j->message = (char *) malloc(sizeof(MESSAGE));
    j->work = work;
    strcpy(j->message, MESSAGE);

    pthread_mutex_lock(jqueue->mutex);

    while (jqueue->jobs->size() >= simul) {
      log(2, "Producer: waiting to add job %d, queue FULL (%d slots filled).", j->id, jqueue->jobs->size());
      pthread_cond_wait(jqueue->notFull, jqueue->mutex);
    }

    log(3, "Producer: Created job %d.", j->id);
    jqueue->jobs->push(j);

    log(
      2, 
      "Producer: Added job %d (%s) to queue, qsize = %d (m = %d bytes)", 
      j->id, 
      j->message, 
      jqueue->jobs->size(), 
      jqueue->jobs->size() * sizeof(job)
    );

    if (i + 1 == total_jobs) {
      log(1, "Producer: Finished, %d total jobs added", i + 1);
      jqueue->producer_finished = true;
    }

    pthread_mutex_unlock(jqueue->mutex);

    if (jqueue->producer_finished) {
      break;
    }

    pthread_cond_signal(jqueue->notEmpty);

    wait = rand() % PRODUCER_WAIT;
    log(3, "Producer wait: %d", wait);

    usleep(wait);
  }

  pthread_cond_broadcast(jqueue->notEmpty);
  pthread_exit(NULL);
}

void *worker(void *args)
{
  int id;
  job_queue *jqueue;
  job *j;
  int wait;
  int jobs_executed = 0;

  id = ((worker_args *) args)->id;
  jqueue = ((worker_args *) args)->jqueue;

  log(1, "Worker[%d]: Starting.", id);

  while (true) {
    log(3, "Worker[%d]: Checking queue.", id);
    pthread_mutex_lock(jqueue->mutex);

    while (jqueue->jobs->empty() && !jqueue->producer_finished) {
      log(2, "Worker[%d]: queue EMPTY.", id);
      pthread_cond_wait(jqueue->notEmpty, jqueue->mutex);
    }

    if (!jqueue->jobs->empty()) {
      j = jqueue->jobs->front();
      jqueue->jobs->pop();

      log(
        2, 
        "Worker[%d]: Removed job %d (%s) from queue, qsize = %d (m = %d bytes)", 
        id,
        j->id, 
        j->message, 
        jqueue->jobs->size(), 
        jqueue->jobs->size() * sizeof(job)
      );

      execute_job(j);
      jobs_executed++;
      job_destroy(j);
    }

    pthread_mutex_unlock(jqueue->mutex);

    if (jqueue->producer_finished && jqueue->jobs->empty()) {
      break;
    }

    pthread_cond_signal(jqueue->notFull);
    
    wait = rand() % WORKER_WAIT;
    log(3, "Worker[%d]: wait = %d", id, wait);
    
    usleep(wait);
  }

  log(1, "Worker[%d]: Queue says Producer is finished, exiting after executing %d jobs.", id, jobs_executed);

  pthread_cond_signal(jqueue->notEmpty);
  pthread_exit(NULL);
}

job_queue *jqueue_init()
{
  job_queue *jqueue = (job_queue *) malloc(sizeof(job_queue));

  jqueue->jobs = new queue<job *>();

  jqueue->mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init (jqueue->mutex, NULL);

  jqueue->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
  pthread_cond_init (jqueue->notFull, NULL);

  jqueue->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
  pthread_cond_init (jqueue->notEmpty, NULL);

  jqueue->producer_finished = false;

  return jqueue;
}

void jqueue_destroy(job_queue *jqueue)
{
  delete jqueue->jobs;
  free(jqueue->mutex);
  free(jqueue->notFull);
  free(jqueue->notEmpty);
  free(jqueue);
}


void job_destroy(job *j)
{
  free(j->message);
  free(j);
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
