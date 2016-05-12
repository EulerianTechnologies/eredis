/**
 * @file eredis.c
 * @brief Eredis example: drop all records without expire
 * @author Guillaume Fougnies <guillaume@eulerian.com>
 * @version 0.1
 * @date 2016-04-01
 */

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "eredis.h"

/*
 * Drop all records without expire
 *
 * Launch a 'SCAN' command, check 'TTL' of each record and
 * issue a DEL if TTL is -1.
 * One reader and async loop: 2 connections.
 *
 * One main function to make it fast to read.
 *
 * Most of the code is reply processing
 */

  int
main( int argc, char *argv[] )
{
  int i, dropped = 0;
  char cursor[32];
  eredis_t *e;
  eredis_reader_t *reader;

  if (argc==1) {
    fprintf(stderr,
            "Need a host as argument, like:\n"
            " %s /var/run/redis.sock\n"
            " %s localhost:6379\n"
            " %s localhost:6379 /var/run/redis.sock\n",
            argv[0], argv[0], argv[0]);
    exit(0);
  }


  /* cancel sigpipe */
  signal(SIGPIPE, SIG_IGN);

  /* eredis */
  e = eredis_new();

  /* host list */
  for (i=1; i<argc; i++) {
    char *tg, *c;
    int port = 0;
    tg = argv[i];
    if ((c = strchr(tg, ':'))) {
      *c = '\0';
      port = atoi(c+1);
    }
    //printf("Host %s (%d)\n", tg, port);
    eredis_host_add( e, tg, port );
  }

  /* Launch the async context */
  eredis_run_thr( e );

  /* a reader */
  reader = eredis_r( e );

  /* cursor start */
  cursor[0] = '0';
  cursor[1] = '\0';
  do {
    eredis_reply_t *scan_reply, **subs;
    size_t k_nb;

    scan_reply = eredis_r_cmd( reader, "SCAN %s", cursor );
    if (! scan_reply) {
      fprintf(stderr, "Failed to connect\n");
      break;
    }

    /* Detach it */
    eredis_r_reply_detach( reader );

    /* Validate the reply */
    if (scan_reply->type != REDIS_REPLY_ARRAY
        ||
        scan_reply->elements != 2
        ||
        !(subs = scan_reply->element)
        ||
        subs[0]->type != REDIS_REPLY_STRING
        ||
        subs[1]->type != REDIS_REPLY_ARRAY)
    {
      fprintf(stderr, "Not compliant reply to SCAN command\n");
      eredis_reply_dump( scan_reply );
      break;
    }

    /* My next cursor in [0], copy '\0' */
    memcpy( cursor, subs[0]->str, subs[0]->len + 1 );

    /* My keys to check in [1], jump to subs */
    k_nb = subs[1]->elements;
    subs = subs[1]->element;

    if (subs) {
      for (i=0; i<k_nb; i++) {
        eredis_reply_t *look_reply;
        char *k;

        k = subs[i]->str;
        if (! k)
          continue;

        /* lookup ttl for this one */
        look_reply = eredis_r_cmd( reader, "TTL %s", k );
        if (
          ! look_reply
          ||
          look_reply->type != REDIS_REPLY_INTEGER)
          continue;

        if (look_reply->integer == -1) {
          /* no expire - drop it in async */
          eredis_w_cmd( e, "DEL %s", k );
          dropped ++;
          /*printf("Dropped: %s\n", k);*/
        }
      }
    }

    /* Drop the detached scan reply */
    eredis_reply_free( scan_reply );

  } while (cursor[0] != '0'); /* iteration ends at 0 */

  printf("%d record(s) dropped\n", dropped);

  eredis_r_release( reader );

  /* Wait for possible async pending commands to end */
  while (eredis_w_pending( e ))
    usleep(1000);

  eredis_free( e );

  return 0;
}

