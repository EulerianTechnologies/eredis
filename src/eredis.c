/*
 * Copyright (c) 2016 by Eulerian Technologies SAS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 *
 * * Neither the name of Eulerian Technologies nor the names of its
 *   contributors may be used to endorse or promote products
 *   derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * @file eredis.c
 * @brief ERedis main
 * @author Guillaume Fougnies <guillaume@eulerian.com>
 * @version 0.1
 * @date 2016-03-29
 */

#define _EREDIS_C  1

/* Embedded hiredis */
#include "async.c"
#include "hiredis.c"
#include "net.c"
#include "read.c"
#include "sds.c"

/* Other */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <ev.h>

/* hiredis ev */
#include "adapters/libev.h"

/* TCP Keep-Alive */
#define HOST_TCP_KEEPALIVE

/* Verbose:
 * 0 => silent
 * 1 => error    (core errors)
 * 2 => +warning (disconnect event)
 * 3 => +debug   (hosts conf and connect event)
 * */
#define EREDIS_VERBOSE    1

/* Retry to connect host every second, 10 times */
#define HOST_DISCONNECTED_RETRIES         10
/* Retry to connect "failed" host every 20 seconds */
#define HOST_FAILED_RETRY_AFTER           20

/* Max readers - DEFAULT */
#define DEFAULT_HOST_READER_MAX           10
/* Timeout - DEFAULT */
#define DEFAULT_HOST_TIMEOUT              5
/* Retry - DEFAULT */
#define DEFAULT_HOST_READER_RETRY         1

/* Number of msg to keep in writer queue if any host is connected */
#define QUEUE_MAX_UNSHIFT                 10000

#define EREDIS_READER_MAX_BUF             (2 * REDIS_READER_MAX_BUF)


/*
 * Host status
 */
/* 0x0f reserved for connection state */
#define HOST_F_DISCONNECTED     0x00
#define HOST_F_CONNECTED        0x01
#define HOST_F_FAILED           0x02
/* 0xf0 reserved for other flags */
#define HOST_F_INIT             0x10
#define HOST_F_CONNECTING       0x20

/* and helpers */
#define H_IS_CONNECTED(h)       (h->status & HOST_F_CONNECTED)
#define H_IS_DISCONNECTED(h)    (h->status & HOST_F_DISCONNECTED)
#define H_IS_FAILED(h)          (h->status & HOST_F_FAILED)
#define H_IS_INIT(h)            (h->status & HOST_F_INIT)
#define H_IS_CONNECTING(h)      (h->status & HOST_F_CONNECTING)

/* Conn state change removes the 'CONNECTING' flag */
#define H_CONN_STATE(h)         h->status & 0x0f
#define _H_SET_STATE(h,f)       h->status = (h->status & \
                                             (0xf0 ^ HOST_F_CONNECTING)) | f

#define H_SET_DISCONNECTED(h)   do {    \
  h->failures = 0;                      \
  _H_SET_STATE(h, HOST_F_DISCONNECTED); } while (0)
#define H_SET_CONNECTED(h)      do {    \
  h->failures = 0;                      \
  _H_SET_STATE(h, HOST_F_CONNECTED);   } while (0)
#define H_SET_FAILED(h)         do {    \
  h->failures = 0;                      \
  _H_SET_STATE(h, HOST_F_FAILED);      } while (0)

#define H_SET_CONNECTING(h)     h->status |= HOST_F_CONNECTING
#define H_UNSET_CONNECTING(h)   h->status &= ~(HOST_F_CONNECTING)
#define H_SET_INIT(h)           h->status |= HOST_F_INIT

/*
 * Misc flags
 */
#define EREDIS_F_INRUN          0x01
#define EREDIS_F_INTHR          0x02
#define EREDIS_F_READY          0x04
#define EREDIS_F_SHUTDOWN       0x08

/* and helpers */
#define IS_INRUN(e)             (e->flags & EREDIS_F_INRUN)
#define IS_INTHR(e)             (e->flags & EREDIS_F_INTHR)
#define IS_READY(e)             (e->flags & EREDIS_F_READY)
#define IS_SHUTDOWN(e)          (e->flags & EREDIS_F_SHUTDOWN)

#define SET_INRUN(e)            e->flags |= EREDIS_F_INRUN
#define SET_INTHR(e)            e->flags |= EREDIS_F_INTHR
#define SET_READY(e)            e->flags |= EREDIS_F_READY
#define SET_SHUTDOWN(e)         e->flags |= EREDIS_F_SHUTDOWN

#define UNSET_INRUN(e)          e->flags &= ~EREDIS_F_INRUN
#define UNSET_INTHR(e)          e->flags &= ~EREDIS_F_INTHR
#define UNSET_READY(e)          e->flags &= ~EREDIS_F_READY
#define UNSET_SHUTDOWN(e)       e->flags &= ~EREDIS_F_SHUTDOWN

/*
 * Host container
 */

struct eredis_s;

typedef struct host_s {
  redisAsyncContext *async_ctx;
  struct eredis_s   *e;
  char              *target;

  /* 'target' is host if port>0 and unix otherwise */
  int               port:16;
  int               status:8;
  /* Connect failure counter:
   * HOST_DISCONNECTED + HOST_DISCONNECTED_RETRIES failure -> HOST_FAILED
   * HOST_FAILED       + HOST_FAILED_RETRY_AFTER -> retry
   */
  int               failures:8;
} host_t;

/*
 * A command
 */
typedef struct cmd_s {
  char                *s;
  int                 l;
} cmd_t;

/*
 * Write Queue of commands
 */
typedef struct wqueue_ent_s {
  struct wqueue_ent_s *next, *prev;
  cmd_t               cmd;
} wqueue_ent_t;

/*
 * Reader container
 */
typedef struct eredis_reader_s {
  struct eredis_reader_s  *next, *prev;
  struct eredis_s         *e;
  redisContext            *ctx;
  void                    *reply;
  host_t                  *host;
  cmd_t                   *cmds;
  int                     cmds_requested; /* delivered requests */
  int                     cmds_replied;   /* delivered replies */
  int                     cmds_nb;
  int                     cmds_alloc;
  int                     free:8;
  int                     retry:8;
} eredis_reader_t;

/*
 * ERedis
 */
typedef struct eredis_s {
  host_t            *hosts;
  int               hosts_nb;
  int               hosts_alloc;
  int               hosts_connected;

  struct timeval    sync_to;
  pthread_mutex_t   reader_lock;
  pthread_cond_t    reader_cond;
  struct {
    eredis_reader_t  *fst;
    int               nb;
  } rqueue;

  int               reader_max;
  int               reader_retry;
  int               flags;

  ev_timer          connect_timer;
  ev_async          send_async;

  int               send_async_pending;
  struct ev_loop    *loop;

  struct {
    wqueue_ent_t     *fst;
    int             nb;
  } wqueue;

  cmd_t             *cmds_connect;    /* post-connect commands */
  int               cmds_connect_nb;

  pthread_t         async_thr;
  pthread_mutex_t   async_lock;
} eredis_t;

/* mine */
#include "eredis.h"

/**
 * Err/Warn/Log
 */
#define _P_ERR(fmt, ...)
#define _P_WARN(fmt, ...)
#define _P_LOG(fmt, ...)
#if defined(EREDIS_VERBOSE)
#if EREDIS_VERBOSE>0
#undef _P_ERR
#define _P_ERR(fmt, ...)    fprintf(stderr,                     \
                                    "eredis: error: "fmt"\n",   \
                                    ##__VA_ARGS__)
#if EREDIS_VERBOSE>1
#undef _P_WARN
#define _P_WARN(fmt, ...)   fprintf(stderr,                     \
                                    "eredis: warning: "fmt"\n", \
                                    ##__VA_ARGS__)
#if EREDIS_VERBOSE>2
#undef _P_LOG
#define _P_LOG(fmt, ...)    fprintf(stdout,                     \
                                   "eredis: log: "fmt"\n",      \
                                   ##__VA_ARGS__)
#endif
#endif
#endif
#endif

/**
 * @brief Build a new eredis environment
 *
 * @return eredis
 */
  eredis_t *
eredis_new( void )
{
  eredis_t *e;

  e = calloc( 1, sizeof(eredis_t) );
  if (!e) {
    _P_ERR( "eredis_new: failed to allocated memory" );
    return NULL;
  }

  e->sync_to.tv_sec = DEFAULT_HOST_TIMEOUT;
  e->reader_max     = DEFAULT_HOST_READER_MAX;
  e->reader_retry   = DEFAULT_HOST_READER_RETRY;

  pthread_mutex_init( &e->async_lock,   NULL );
  pthread_mutex_init( &e->reader_lock,  NULL );
  pthread_cond_init(  &e->reader_cond,  NULL );

  return e;
}

/**
 * @brief Set timeout for all redis connections
 *
 * Default is DEFAULT_HOST_TIMEOUT (5 seconds)
 *
 * @param e          eredis
 * @param timeout_ms timeout in milliseconds
 */
  void
eredis_timeout( eredis_t *e, int timeout_ms )
{
  e->sync_to.tv_sec  = timeout_ms / 1000;
  e->sync_to.tv_usec = (timeout_ms % 1000) * 1000;
}

/**
 * @brief Set max number of reader
 *
 * Default is DEFAULT_HOST_READER_MAX (10)
 *
 * @param e   eredis
 * @param max max number of reader
 */
  void
eredis_r_max( eredis_t *e, int max )
{
  e->reader_max = max;
}

/**
 * @brief Set reader max retry
 *
 * Default is DEFAULT_HOST_READER_RETRY (1)
 *
 * @param e     eredis
 * @param retry number of retry
 */
  void
eredis_r_retry( eredis_t *e, int retry )
{
  e->reader_retry = retry;
}

/**
 * @brief Add a post-connect command
 *
 *
 */
  int
eredis_pc_cmd( eredis_t *e, const char *fmt, ... )
{
  int i;
  size_t l;
  char *cmd = NULL;
  va_list ap;

  va_start(ap,fmt);
  l = redisvFormatCommand( &cmd, fmt, ap );
  va_end(ap);
  if (!cmd)
    return EREDIS_ERRCMD;
  if (l<=0) {
    free(cmd);
    return EREDIS_ERRCMD;
  }

  i = e->cmds_connect_nb;
  e->cmds_connect = realloc( e->cmds_connect, sizeof(cmd_t) * (i+1) );

  e->cmds_connect[ i ].s = cmd;
  e->cmds_connect[ i ].l = l;
  e->cmds_connect_nb ++;

  return EREDIS_OK;
}

/**
 * @brief Add a host to eredis
 *
 * Must be called after 'new' and before any call to 'run'.
 * The first added host will be the reference host for reader.
 *
 * If a dead 'first host' become unavailable, reader's requests
 * will switch to any other host available.
 *
 * When the dead 'first host' come back to life, reader's requests
 * will switch back to it.
 *
 * @param e       eredis
 * @param target  hostname, ip or unix socket
 * @param port    port number (0 to activate unix socket)
 * @return        -1 on error, 0 on success
 */
  int
eredis_host_add( eredis_t *e, char *target, int port )
{
  host_t *h;
  if (e->hosts_nb <= e->hosts_alloc) {
    host_t *ah;
    e->hosts_alloc += 8;
    ah = realloc( e->hosts, sizeof(host_t) * e->hosts_alloc );
    if (! ah) {
      _P_ERR("eredis_host_add: failed to reallocate");
      return -1;
    }
    e->hosts = ah;
  }

  _P_LOG("adding host: %s (%d)", target, port);

  h             = &e->hosts[ e->hosts_nb ];
  h->async_ctx  = NULL;
  h->e          = e;
  h->target     = strdup( target );
  if (! h->target) {
    _P_ERR("eredis_host_add: failed to allocate target");
    return -1;
  }

  h->port       = port;
  h->status     = 0;

  H_SET_DISCONNECTED( h );

  e->hosts_nb ++;

  return 0;
}

/**
 * @brief Quick and dirty host file loader
 *
 * The file can contain comments (starting '#').
 * One line per target.
 * Hostname and port must be separated by ':'.
 * Unix sockets do not take any port value.
 *
 * @param e     eredis
 * @param file  host list file
 *
 * @return number of host loaded, -1 on error
 */
  int
eredis_host_file( eredis_t *e, const char *file )
{
  struct stat st;
  int fd;
  int ret = -1, len;
  char *bufo = NULL,*buf;

  fd = open( file, O_RDONLY );
  if (fd<0)
    return -1;

  if (fstat( fd, &st ))
    goto out;

  len = st.st_size;
  if (len > 0x1<<16) /* strange > 64K */
    goto out;

  bufo = buf = (char*) malloc(sizeof(char)*(len+1));
  if (! buf) {
    _P_ERR("eredis_host_file: failed to allocate");
    return -1;
  }
  len = read( fd, buf, len );
  if (len != st.st_size)
    goto out;

  ret = 0;
  *(buf + len) = '\0';
  while (*buf) {
    int port = 0;
    char *tk, *end = strchr(buf,'\n');
    if (! end)
      end = buf + strlen(buf);
    buf += strspn(buf, " \t");
    if (buf == end || *buf == '#')
      goto next;
    tk = end;
    while (tk>buf && (*tk == ' ' || *tk == '\t'))
      tk --;
    *tk = '\0';
    tk = strrchr(buf, ':'); /* port */
    if (tk) {
      *tk = '\0';
      port = atoi(tk+1);
    }
    eredis_host_add( e, buf, port );
    ret ++;
next:
    buf = end+1;
  }

out:
  close(fd);
  if (bufo)
    free(bufo);
  return ret;
}

/* Embedded reader code */
#include "reader.c"
/* Embedded queue code */
#include "queue.c"

/* Redis - ev - connect callback */
  static void
_redis_connect_cb (const redisAsyncContext *c, int status)
{
  host_t *h = (host_t*) c->data;

  H_SET_INIT( h );

  if (status == REDIS_OK) {
    _P_LOG("connect_cb: connected %s", h->target);

    H_SET_CONNECTED( h );

    h->e->hosts_connected ++;

    return;
  }

  /* Unset connecting flag */
  H_UNSET_CONNECTING( h );

  _P_LOG("connect_cb: failed %s", h->target);

  /* Status increments */
  switch (H_CONN_STATE( h )) {
    case HOST_F_DISCONNECTED:
      if ((++ h->failures) > HOST_DISCONNECTED_RETRIES) {
        _P_WARN("host to failed: %s", h->target);
        H_SET_FAILED( h );
      }
      break;

    case HOST_F_FAILED:
      h->failures = 0;
      break;
  }

  /* Free is taken care by hiredis - unlink */
  h->async_ctx  = NULL;
}

/* Redis - ev - disconnect callback */
  static void
_redis_disconnect_cb (const redisAsyncContext *c, int status)
{
  host_t *h = (host_t*) c->data;

  (void)status;

  _P_WARN("disconnect_cb: %s", h->target);

  if (! H_IS_CONNECTED(h)) {
    _P_ERR(
      "strange behavior: "
      "redis_disconnect_cb called on !HOST_CONNECTED");
  }
  else
    h->e->hosts_connected --;

  h->async_ctx  = NULL;
  H_SET_DISCONNECTED( h );
  /* Free is take care by hiredis */
}

/* Internal host connect - Sync or Async */
  static inline redisContext *
_host_connect_sync( host_t *h, eredis_reader_t *r )
{
  int i;
  eredis_t *e;
  redisContext *c;

  e = h->e;

  c = (h->port) ?
    redisConnect( h->target, h->port )
    :
    redisConnectUnix( h->target );

  if (! c) {
    _P_ERR("connect sync %s NULL", h->target);
    return NULL;
  }
  if (c->err) {
    _P_LOG("connect sync failed %s err:%d", h->target, c->err);
    redisFree( c );
    return NULL;
  }

  r->ctx   = c;
  r->host  = h;

  /* Process post-connect command if any */
  for (i=0; i<e->cmds_connect_nb; i++) {
    redisAppendFormattedCommand( r->ctx,
                                 e->cmds_connect[i].s,
                                 e->cmds_connect[i].l );
  }

  for (i=0; i<e->cmds_connect_nb; i++) {
    eredis_reply_t *reply = NULL;
    int err;
    err = redisGetReply( r->ctx, (void**)&reply );
    if (reply)
      freeReplyObject( reply );
    if (err != EREDIS_OK) {
      _P_ERR( "eredis_reader: failed to execute post-connect cmd: %.*s",
              e->cmds_connect[i].l,
              e->cmds_connect[i].s );
      return NULL;
    }
  }

  return c;
}

  static inline redisContext *
_host_connect_async( host_t *h )
{
  int i;
  eredis_t *e;
  redisAsyncContext *ac;

  e = h->e;

  /* ASync - in EV context */
  ac = (h->port) ?
    redisAsyncConnect( h->target, h->port )
    :
    redisAsyncConnectUnix( h->target );

  _P_LOG("connecting async %s", h->target);

  if (! ac) {
    _P_ERR( "connect async %s undef", h->target);
    return NULL;
  }
  if (ac->err) {
    H_SET_INIT( h );
    _P_LOG( "connect async failed %s err:%d", h->target, ac->err);
    redisAsyncFree( ac );
    return NULL;
  }

  if (h->async_ctx)
    redisAsyncFree( h->async_ctx );

  h->async_ctx = ac;

  /* data for _redis_*_cb */
  ac->data = h;

  /* Order is important here */

  /* attach */
  redisLibevAttach( e->loop, ac );

  /* set callbacks */
  redisAsyncSetDisconnectCallback( ac, _redis_disconnect_cb );
  redisAsyncSetConnectCallback( ac, _redis_connect_cb );

  /* set connecting flag */
  H_SET_CONNECTING( h );

  /* Append post-connect command if any */
  for (i=e->cmds_connect_nb - 1; i>=0; i--) {
    char *cmd = strndup( e->cmds_connect[i].s, e->cmds_connect[i].l );
    if (! cmd) {
      H_SET_DISCONNECTED( h );
      redisAsyncFree( h->async_ctx );
      h->async_ctx = NULL;
      return NULL;
    }
    _eredis_wqueue_unshift( e, cmd, e->cmds_connect[i].l );
  }

  return (redisContext*) ac;
}

  static int
_host_connect( host_t *h, eredis_reader_t *r )
{
  redisContext *c;

  /* Connect per context, reader => sync, writer => async */
  c = (r) ? _host_connect_sync( h, r ) : _host_connect_async( h );
  if (! c)
    return 0;

  /* Apply keep-alive */
#ifdef HOST_TCP_KEEPALIVE
  if (h->port) {
    redisEnableKeepAlive( c );
    if (r && (h->e->sync_to.tv_sec || h->e->sync_to.tv_usec)) {
      redisSetTimeout( c, h->e->sync_to );
    }
  }
#endif

  /* Override the maxbuf */
  c->reader->maxbuf = EREDIS_READER_MAX_BUF;

  return 1;
}

/*
 * EV send callback
 *
 * EV_ASYNC send_async
 */
  static void
_eredis_ev_send_cb (struct ev_loop *loop, ev_async *w, int revents)
{
  int i, nb, l;
  char *s;
  eredis_t *e;

  (void) revents;
  (void) loop;

  e = (eredis_t*) w->data;

  e->send_async_pending = 0;

  while ((s = _eredis_wqueue_shift( e, &l ))) {
    for (nb = 0, i=0; i<e->hosts_nb; i++) {
      host_t *h = &e->hosts[i];

      if (H_IS_CONNECTED(h)) {
        __redisAsyncCommand( h->async_ctx, NULL, NULL, s, l );
        nb ++;
      }
    }

    if (
      (! nb)  /* failed to deliver to any host */
      &&
      (e->wqueue.nb < QUEUE_MAX_UNSHIFT))
    {
      /* Unshift and stop */
      _eredis_wqueue_unshift( e, s, l );
      break;
    }

    free( s );
  }
}

/*
 * EV send async trigger for new commands to send
 * (External to the event loop)
 */
  static inline void
_eredis_ev_send_trigger (eredis_t *e)
{
  if (IS_READY(e) && !IS_SHUTDOWN(e) && !e->send_async_pending) {
    e->send_async_pending = 1;
    ev_async_send( e->loop, &e->send_async );
  }
}

/*
 * EV connect callback
 *
 * EV_TIMER connect_timer
 */
  static void
_eredis_ev_connect_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
  int i;
  eredis_t *e;

  (void) revents;
  (void) loop;

  e = (eredis_t*) w->data;

  /* Shutdown procedure */
  if (IS_SHUTDOWN(e)) {
    if (e->hosts_connected) {
      int nb = 0;
      for (i=0; i<e->hosts_nb; i++) {
        host_t *h = &e->hosts[i];
        if (h->async_ctx) {
          if (H_IS_CONNECTED(h)) {
            nb ++;
            redisAsyncDisconnect( h->async_ctx );
          }
        }
      }
      e->hosts_connected = nb;
    }
    else {
      /* Connect timer */
      ev_timer_stop( e->loop, &e->connect_timer );
      /* Async send */
      ev_async_stop( e->loop, &e->send_async );
      /* Event break */
      ev_break( e->loop, EVBREAK_ALL );
    }

    return;
  }

  /* Normal procedure */
  for (i=0; i<e->hosts_nb; i++) {
    host_t *h = &e->hosts[i];

    if (H_IS_CONNECTING( h )) {
      /* avoid host with 'connecting' flag */
      continue;
    }

    switch (H_CONN_STATE( h )) {
      case HOST_F_CONNECTED:
        break;

      case HOST_F_FAILED:
        if ((h->failures < HOST_FAILED_RETRY_AFTER)
            ||
            (! _host_connect( h, 0 ))) {
          h->failures %= HOST_FAILED_RETRY_AFTER;
          h->failures ++;
        }
        break;

      case HOST_F_DISCONNECTED:
        if (! _host_connect( h, 0 )) {
          if ((++ h->failures) > HOST_DISCONNECTED_RETRIES) {
            H_SET_FAILED( h );
          }
        }
        break;

      default:
        break;
    }
  }

  if (! IS_READY(e)) {
    /* Ready flag - need a connected host or a connection failure */
    int nb = 0;
    /* build ready flag */
    for (i=0; i<e->hosts_nb; i++) {
      host_t *h = &e->hosts[i];
      if (H_IS_INIT( h ))
        nb ++;
    }
    if (nb == e->hosts_nb) {
      SET_READY(e);
      e->send_async_pending = 1;
      ev_async_send( e->loop, &e->send_async );
    }
  }
}

/*
 * Internal generic eredis runner for the event loop (write)
 */
  static void
_eredis_run( eredis_t *e, int flags )
{
  if (! e->loop) {
    ev_timer *levt;
    ev_async *leva;

    e->loop = ev_loop_new( EVFLAG_AUTO );

    /* Connect timer */
    levt = &e->connect_timer;
    ev_timer_init( levt, _eredis_ev_connect_cb, 0., 1. );
    levt->data = e;
    ev_timer_start( e->loop, levt );

    /* Async send */
    leva = &e->send_async;
    ev_async_init( leva, _eredis_ev_send_cb );
    leva->data = e;
    ev_async_start( e->loop, leva );
  }

  SET_INRUN(e);

  if (IS_INTHR(e))
    /* Thread mode - release the thread creator */
    pthread_mutex_unlock( &(e->async_lock) );

  ev_run( e->loop, flags );

  UNSET_INRUN(e);
}


/**
 * @brief run eredis event loop (for writes) in blocking mode
 *
 * The loop will be stopped by a call to 'eredis_shutdown' or
 * 'eredis_free'.
 * From another thread or a signal.
 *
 * @param e eredis
 *
 * @return 0 on success
 */
  int
eredis_run( eredis_t *e )
{
  _eredis_run( e, 0 );
  return 0;
}

  static void *
_eredis_run_thr( void *ve )
{
  eredis_t *e = ve;
  SET_INTHR( e );
  _eredis_run( e, 0 );
  UNSET_INTHR( e );
  pthread_exit( NULL );
}

/**
 * @brief run eredis event loop (for writes) in a dedicated thread
 *
 * Will block until the thread is ready.
 *
 * @param e eredis
 *
 * @return 0 on success
 */
  int
eredis_run_thr( eredis_t *e )
{
  int err = 0;

  if (IS_INTHR(e))
    return 0;

  pthread_mutex_lock( &(e->async_lock) );

  if (! IS_INRUN(e)) {
    err = (int)
      pthread_create( &e->async_thr, NULL, _eredis_run_thr, (void*)e );

    if (0 == err) {
      while (! IS_INRUN(e))
        /* Trigger from running thread */
        pthread_mutex_lock( &(e->async_lock) );
    }
  }

  pthread_mutex_unlock( &(e->async_lock) );

  return err;
}


  void
eredis_reply_detach( eredis_reader_t *reader )
{
  reader->reply = NULL;
}

/**
 * @brief Dump eredis reply (hiredis format).
 *
 * Helper function:
 * Save time !
 *
 * @param reply   eredis reply
 */
  static void
_eredis_reply_dump( eredis_reply_t *reply, int depth )
{
  int i, indent = (depth+1) * 2;

  if (! reply)
    return;

  switch (reply->type) {
    case REDIS_REPLY_NIL:
      printf( "%*c%s\n", indent, ' ', "Nil");
      break;

    case REDIS_REPLY_INTEGER:
      printf( "%*c%s : %lld\n", indent, ' ', "Integer", reply->integer);
      break;

    case REDIS_REPLY_STRING:
      printf( "%*c%s : \"%.*s\"\n", indent, ' ', "String",
              (int)reply->len, reply->str);
      break;

    case REDIS_REPLY_ARRAY:
      printf( "%*c%s  : %zu\n", indent, ' ', "Array", reply->elements);
      for (i=0; i<reply->elements; i++) {
        _eredis_reply_dump( reply->element[i], depth+1 );
      }
      break;

    case REDIS_REPLY_STATUS:
      printf( "%*c%s : %.*s\n", indent, ' ', "Status",
              (int)reply->len, reply->str);
      break;

    case REDIS_REPLY_ERROR:
      printf( "%*c%s  : %.*s\n", indent, ' ', "Error",
              (int)reply->len, reply->str);
      break;

    default:
      printf( "%*c%s\n", indent, ' ', "Unknown type?!");
      break;
  }
}

  void
eredis_reply_dump( eredis_reply_t *reply )
{
  printf( "eredis: dump: %p\n", reply );
  _eredis_reply_dump( reply, 0 );
  printf( "/eredis: dump\n");
}

/**
 * @brief Free a reply (must be detached)
 *
 * Simple wrapper around hiredis 'freeReplyObject'
 *
 * @param reply a detached reply
 */
  void
eredis_reply_free( eredis_reply_t *reply )
{
  freeReplyObject( reply );
}

/**
 * @brief Shutdown the event loop
 *
 * @param e eredis
 */
  void
eredis_shutdown( eredis_t *e )
{
  /* Flag for shutdown */
  SET_SHUTDOWN(e);
}

/**
 * @brief Stop eredis and free all ressources allocated
 *
 * @param e eredis
 */
  void
eredis_free( eredis_t *e )
{
  int i;
  char *s;
  eredis_reader_t *r;

  /* Flag for shutdown */
  SET_SHUTDOWN(e);

  /* Shutdown per hosts */
  if (e->hosts) {
    for (i=0; i<e->hosts_nb; i++) {
      host_t *h = &e->hosts[i];
      if (h->async_ctx) {
        redisAsyncDisconnect( h->async_ctx );
        if (! IS_INTHR( e ))
          _eredis_run( e, EVRUN_NOWAIT );
      }
    }
  }

  /* Loop trash */
  if (e->loop) {
    if (IS_INTHR( e )) /* Thread - wait to EVBREAK_ALL */
      pthread_join( e->async_thr, NULL );
    else /* Re-run loop until EVBREAK_ALL */
      eredis_run( e );

    ev_loop_destroy( e->loop );
    e->loop = NULL;
  }

  if (e->hosts) {
    for (i=0; i<e->hosts_nb; i++) {
      host_t *h = &e->hosts[i];
      if (h->async_ctx) {
        redisAsyncFree( h->async_ctx );
        h->async_ctx = NULL;
      }
      if (h->target)
        free(h->target);
    }
    free(e->hosts);
    e->hosts = NULL;
  }

  /* Clear rqueue */
  while ((r = _eredis_rqueue_shift( e ))) {
    if (r->free) {
      _eredis_reader_free( r );
    }
    else {
      _P_ERR("eredis_free: reader not in 'free' state!?");
    }
  }

  /* Clear wqueue */
  while ((s = _eredis_wqueue_shift( e, NULL )))
    free(s);

  pthread_mutex_destroy( &e->async_lock );
  pthread_mutex_destroy( &e->reader_lock );
  pthread_cond_destroy( &e->reader_cond );

  /* Clear post-connect commands */
  if (e->cmds_connect) {
    for (i=0; i<e->cmds_connect_nb; i++) {
      free( e->cmds_connect[i].s );
    }
    free( e->cmds_connect );
  }

  free(e);
}

/* Embedded rw code */
#include "rw.c"

