
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "eredis.h"

static eredis_t *e;
static enum echeck_t {
  CHECK_RUNNING,
  CHECK_SHUTDOWN,
  CHECK_ERROR
} c;
struct sigaction old_action;

void sigint_handler(int sig_no)
{
  (void)sig_no;

  printf("CTRL-C pressed\n");
  sigaction(SIGINT, &old_action, NULL);
  if (e) {
    c = CHECK_SHUTDOWN;
    eredis_shutdown( e );
  }
}

void* pending_check(void *ctx)
{
  assert( ctx == e );

  c = CHECK_RUNNING;
  while (c == CHECK_RUNNING && eredis_w_pending( e ) > 0) {
    sleep(1);
  }

  if(e && c != CHECK_SHUTDOWN) {
    eredis_shutdown( e );
  }

  return NULL;
}

  int
main( int argc, char *argv[] )
{
  /* optional command line arguments */
  const char *host_file = "test-hosts.conf";
  if (argc >= 2) {
    host_file = argv[1];
  }

  /* to interrupt */
  struct sigaction action;
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

  /* eredis_pc_cmd( e, "AUTH mysecret" ); */

  int i,j,f;
  char fmt[32];
  for (j='a', f=0; j<='b'; j++) {
    printf("Batch letter %c\n", j);
    for (i=0; i<10000;i++) {
      sprintf(fmt, "%c%d", j, i );
      if (eredis_w_cmd( e, "SET %s 100", fmt ) != EREDIS_OK)
        ++f;
    }
  }

  if (f>0) {
    fprintf(stderr, "Failed to eredis_w_cmd %dx\n", f);
  }

  /* check for the shutdown condition */
  pthread_t t;
  pthread_create( &t, NULL, &pending_check, e );

  printf("Blocking run, processing cmds, CTRL-C when you want to exit\n");
  eredis_run( e );

  c = CHECK_SHUTDOWN;
  pthread_join( t, NULL );

  eredis_free( e );

  return 0;
}

