
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
main( void )
{
  int err;
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
  if (eredis_host_file( e, "test-hosts.conf" )<=0) {
    fprintf(stderr, "Unable to load conf\n");
    exit(1);
  }

  /* eredis_pc_cmd( e, "AUTH mysecret" ); */

  /* run thread (not mandatory for reader-only) */
  //eredis_run_thr( e );

  /* get a reader */
  eredis_reader_t *reader = eredis_r( e );

  int i,j,nb = 0;
  char fmt[32];
  for (j='a'; j<='b'; j++) {
    printf("Batch letter %c\n", j);
    for (i=0; i<2;i++) {
      eredis_reply_t *reply;

      sprintf(fmt, "%c%d", j, i );

      err = eredis_r_append_cmd( reader, "GET %s", fmt);
      if (err != EREDIS_OK)
        fprintf(stderr, "Err on eredis_r_append_cmd: %d\n", err);

      reply = eredis_r_cmd( reader, "GET %s", fmt );
      if (reply) {
        nb ++;
        //fprintf(stderr, "got reply: %s (subs:%d)\n", reply->str, reply->elements);
      }
      else {
        fprintf(stderr, "Err on eredis_r_cmd\n");
      }

      reply = eredis_r_reply( reader );
      if (reply) {
        nb ++;
        //fprintf(stderr, "got reply: %s (subs:%d)\n", reply->str, reply->elements);
      }

      eredis_r_clear( reader );
    }
  }

  eredis_r_release( reader );

  eredis_free( e );

  fprintf(stderr, "Got: %d replies\n", nb);

  return 0;
}

