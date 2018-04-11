
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "eredis.h"

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

  /* run thread */
  eredis_run_thr( e );

  int i,j,f;
  char fmt[32];
  for (j='a', f=0; j<='d'; j++) {
    printf("Batch letter %c\n", j);
    for (i=0; i<10000;i++) {
      sprintf(fmt, "%c%d", j, i );
      if (eredis_w_cmd( e, "SET %s %i", fmt, i ) != EREDIS_OK)
        ++f;
    }
  }

  if (f > 0) {
    fprintf(stderr, "Failed to eredis_w_cmd %dx\n", f);
  }

  /* Let some time to process... normal run... yield a bit... push more write... etc.. */
  printf("Sleeping.. async running, CTRL-C when you want to exit\n");
  while (eredis_w_pending( e ) > 0) {
    sleep(1);
  }

  eredis_free( e );

  return 0;
}

