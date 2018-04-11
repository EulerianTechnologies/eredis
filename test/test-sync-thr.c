
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#include "eredis.h"

#define THR_NB 30

static eredis_t *e;
struct sigaction old_action;

void sigint_handler(int sig_no)
{
  (void)sig_no;

  printf("CTRL-C pressed\n");
  sigaction(SIGINT, &old_action, NULL);
  if (e) {
    eredis_shutdown( e );
  }
}


static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int throk = 0;
static int nbtot = 0;

  void *
reader_thr( void *ve )
{
  (void)ve;

  /* Stupid synchro trigger */
  __sync_fetch_and_add( &throk, 1 );

  pthread_mutex_lock(&lock); pthread_mutex_unlock(&lock);

  int i,j;
  char fmt[32];
  for (j='a'; j<='b'; j++) {
    printf("Batch letter %c\n", j);
    for (i=0; i<10000;i++) {
      sprintf(fmt, "%c%d", j, i );

      /* Stress queue - get a new one at each loop */
      eredis_reader_t *reader = eredis_r( e );
      eredis_reply_t *reply;

      if (! reader)
        fprintf(stderr, "Err failed to get reader\n");
      
      reply = eredis_r_cmd( reader, "GET %s", fmt );
      if (reply) {
        __sync_fetch_and_add( &nbtot, 1 );
#if 0
        if (reply->elements) {
          int x;
          struct redisReply **element = reply->element;
          for (x = 0; x < reply->elements; x ++) {
            //fprintf(stderr, ">%s\n",element[i]->str);
          }
        }
#endif
      }
      else {
        fprintf(stderr, "Err on eredis_r_cmd\n");
      }

      eredis_r_release( reader );
    }
  }

  pthread_exit(NULL);
}


  int
main( int argc, char *argv[] )
{
  /* optional command line arguments */
  const char *host_file = "test-hosts.conf";
  if (argc >= 2) {
    host_file = argv[1];
  }

  int i;
  struct sigaction action;
  pthread_t thrs[ THR_NB ];

  memset(&action, 0, sizeof(action));
  action.sa_handler = &sigint_handler;
  sigaction(SIGINT, &action, &old_action);

  /* cancel sigpipe */
  signal(SIGPIPE, SIG_IGN);

  /* eredis */
  e = eredis_new();

  /* conf */
  if (eredis_host_file( e, host_file )<=0) {
    fprintf(stderr, "Unable to load conf %s\n", host_file);
    exit(1);
  }

  /* run thread (not mandatory for reader-only) */
  /* but mandatory for auto reconnect to prefered host */
  eredis_run_thr( e );

  pthread_mutex_lock( &lock );

  for (i=0; i<THR_NB; i++) {
    pthread_create( &thrs[i], NULL, reader_thr, e );
  }

  /* Stupid wait for all thread to be ready */
  while (throk != THR_NB)
    usleep(10);

  /* Unlock cascade */
  pthread_mutex_unlock( &lock );

  /* Wait to terminate */
  for (i=0; i<THR_NB; i++) {
    pthread_join( thrs[i], NULL );
  }

  eredis_free( e );

  fprintf(stderr, "Got: %d replies\n", nbtot);

  return 0;
}

