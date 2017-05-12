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
 * @file rw.c
 * @brief ERedis rw
 * @author Guillaume Fougnies <guillaume@eulerian.com>
 * @version 0.1
 * @date 2016-03-29
 */

/*
 * Process
 */

#define SAN_CMD()       \
  do{if(!cmd || len<=0)return EREDIS_ERRCMD;}while(0)
#define SAN_CMD_FREE()            \
  do{                             \
    if(!cmd)return EREDIS_ERRCMD; \
    if(len<=0){free(cmd);return EREDIS_ERRCMD;}}while(0)

/**
 * @brief eredis write formatted command
 *
 * On success (EREDIS_OK), eredis is responsible of freeing the given 'command'
 *
 * @param e   eredis
 * @param cmd command
 * @param len length of command
 *
 * @return EREDIS_ERRCMD, EREDIS_OK
 */
  int
eredis_w_fcmd( eredis_t *e, const char *cmd, size_t len )
{
  SAN_CMD();

  _eredis_wqueue_push( e, (char*)cmd, len );
  _eredis_ev_send_trigger( e );

  return EREDIS_OK;
}

/**
 * @brief eredis write vargs command
 *
 * @param e   eredis
 * @param fmt format
 * @param ap  vargs
 *
 * @return EREDIS_ERRCMD, EREDIS_OK
 */
  int
eredis_w_vcmd( eredis_t *e, const char *fmt, va_list ap )
{
  size_t len;
  char *cmd = NULL;

  len = redisvFormatCommand( &cmd, fmt, ap );
  SAN_CMD_FREE();

  return eredis_w_fcmd( e, cmd, len );
}

/**
 * @brief eredis write 'printf' style command
 *
 * @param e   eredis
 * @param fmt format
 * @param ... list
 *
 * @return EREDIS_ERRCMD, EREDIS_OK
 */
  int
eredis_w_cmd( eredis_t *e, const char *fmt, ... )
{
  int err;
  va_list ap;

  va_start(ap,fmt);
  err = eredis_w_vcmd( e, fmt, ap );
  va_end(ap);

  return err;
}

/**
 * @brief eredis write argc/argv command
 *
 * @param e       eredis
 * @param argc    argument count
 * @param argv    argument vector
 * @param argvlen argument length vector
 *
 * @return EREDIS_ERR or EREDIS_OK
 */
  int
eredis_w_cmdargv( eredis_t *e,
                  int argc, const char **argv, const size_t *argvlen )
{
  size_t len;
  char *cmd = NULL;

  len = redisFormatCommandArgv(&cmd,argc,argv,argvlen);
  SAN_CMD_FREE(); /* precheck to avoid EREDIS_ERRCMD in fcmd */

  return eredis_w_fcmd( e, cmd, len );
}


/**
 * @brief eredis write queue pending commands
 *
 * @param e     eredis
 *
 * @return number of pending commands
 */
  int
eredis_w_pending( eredis_t *e )
{
  return e->wqueue.nb;
}

/*
 * READ - sync - to first available host
 */

/*
 * free reader's reply.
 * Obviously the previous processed reply.
 */
  static inline void
_eredis_r_free_reply( eredis_reader_t *r )
{
  if (r->reply) {
    freeReplyObject( r->reply );
    r->reply = NULL;
  }
}

/*
 * get or disconnect the reader context
 * Manage the reconnection to the prefered host.
 */
  static redisContext*
_eredis_r_ctx( eredis_reader_t *r, int disconnect )
{
  int i;
  host_t *h;
  eredis_t *e = r->e;

  /* Got one already connected */
  if (r->ctx)
    goto out;

  /* Del asked.. go for drop */
  if (disconnect)
    goto out;

  /* Check for an active server in async_ctx and connect */
  if (IS_READY( e )) {
    for (i=0; i<e->hosts_nb; i++) {
      h = &e->hosts[i];
      if (H_IS_CONNECTED(h) && _host_connect( h, r ))
        goto out;
    }
  }

  /* Fallback, try to connect anyway */
  for (i=0; i<e->hosts_nb; i++) {
    h = &e->hosts[i];
    if (_host_connect( h, r ))
      goto out;
  }

  return NULL;

out:
  if (! r->ctx)
    return NULL;

  _eredis_r_free_reply( r );

  if (disconnect) {
    redisFree( r->ctx );
    r->ctx   = NULL;
    r->host  = NULL;
  }

  return r->ctx;
}

/*
 * get one reader
 */

/**
 * @brief eredis reader pop
 *
 * The reader obtained must be released via 'eredis_r_release'
 *
 * @param e eredis
 *
 * @return eredis reader
 */
  eredis_reader_t *
eredis_r( eredis_t *e )
{
  return _eredis_rqueue_get( e );
}

/*
 * Add one cmd in reader
 */
  static inline int
_eredis_r_add( eredis_reader_t *r, char *cmd, int len )
{
  rcmd_t *rcmd;

  if (r->cmds_nb>=r->cmds_alloc) {
    r->cmds_alloc += 8;
    r->cmds = realloc( r->cmds, sizeof(rcmd_t) * r->cmds_alloc );
  }

  rcmd = &r->cmds[ r->cmds_nb ++ ];
  rcmd->s = cmd;
  rcmd->l = len;

  return EREDIS_OK;
}

/**
 * @brief eredis reader clear
 *
 * Can be called manually to clear any pending commands in reader.
 * It could happen in case of multiple command append and partial reply
 * retrieve.
 *
 * @param r eredis reader
 */
  void
eredis_r_clear( eredis_reader_t *r )
{
  int i;

  for (i=r->cmds_replied; i<r->cmds_nb; i++)
    if (eredis_r_reply( r ) != EREDIS_OK)
      break;

  for (i=0; i<r->cmds_nb; i++)
    free(r->cmds[i].s);

  r->cmds_nb = r->cmds_requested = r->cmds_replied = 0;
}

/**
 * @brief eredis reader release
 *
 * Release a reader obtained via eredis_r
 *
 * @param r eredis reader
 */
  void
eredis_r_release( eredis_reader_t *r )
{
  host_t *h;

  /* Clear */
  eredis_r_clear( r );

  /* Disconnect if the prefered host is available.
   * Flag is triggered by event loop */
  if ((h = &r->e->hosts[0]) &&
      r->host &&
      r->host != h &&
      H_IS_CONNECTED(h))
    _eredis_r_ctx( r, 1 );

  /* Release in queue */
  _eredis_rqueue_release( r );
}

/**
 * @brief eredis read append formatted command (pipelining)
 *
 * On success (EREDIS_OK), eredis is responsible of freeing the given 'command'
 *
 * @param r       eredis reader
 * @param cmd     command
 * @param len     length of command
 *
 * @return EREDIS_ERRCMD, EREDIS_ERR, EREDIS_OK
 */
  int
eredis_r_append_fcmd( eredis_reader_t *r, const char *cmd, size_t len )
{
  SAN_CMD();

  return _eredis_r_add( r, (char*)cmd, len );
}

/**
 * @brief eredis read append vargs command (pipelining)
 *
 * @param r       eredis reader
 * @param format  command
 * @param ap      length of command
 *
 * @return EREDIS_ERRCMD, EREDIS_ERR, EREDIS_OK
 */
  int
eredis_r_append_vcmd( eredis_reader_t *r, const char *format, va_list ap )
{
  int len;
  char *cmd = NULL;

  len = redisvFormatCommand( &cmd, format, ap );

  SAN_CMD_FREE();

  return _eredis_r_add( r, cmd, len );
}

/**
 * @brief eredis read append 'printf' style command (pipelining)
 *
 * @param r       eredis reader
 * @param format  format
 * @param ...     list
 *
 * @return EREDIS_ERRCMD, EREDIS_ERR, EREDIS_OK
 */
  int
eredis_r_append_cmd( eredis_reader_t *r, const char *format, ... )
{
  va_list ap;
  int ret;

  va_start(ap,format);
  ret = eredis_r_append_vcmd( r, format, ap );
  va_end(ap);

  return ret;
}

/**
 * @brief eredis read append argc/argv command
 *
 * @param r       eredis reader
 * @param argc    argument count
 * @param argv    argument vector
 * @param argvlen argument length vector
 *
 * @return EREDIS_ERRCMD, EREDIS_ERR, EREDIS_OK
 */
  int
eredis_r_append_cmdargv( eredis_reader_t *r,
                         int argc, const char **argv, const size_t *argvlen)
{
  size_t len;
  char *cmd = NULL;

  len = redisFormatCommandArgv(&cmd,argc,argv,argvlen);
  SAN_CMD_FREE();

  return eredis_r_append_fcmd( r, cmd, len );
}

/* with reply */

/*
 * eredis reader context with command append
 */
  static inline int
_eredis_r_send( eredis_reader_t *r, redisContext **pc )
{
  int i;
  redisContext *c;

  if (!(c = _eredis_r_ctx(r, 0)))
    return EREDIS_ERR;

  *pc = c;
  for (i=r->cmds_requested; i<r->cmds_nb; i++)
    redisAppendFormattedCommand( c, r->cmds[i].s, r->cmds[i].l );

  return EREDIS_OK;
}

/**
 * @brief eredis reader reply, blocking for pub/sub
 *
 * @param r     eredis reader
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_reply_blocking( eredis_reader_t *r )
{
    redisContext *c;
    eredis_reply_t *reply;
    int err;

    reply = NULL;

    if (!(c = _eredis_r_ctx(r, 0)))
        return NULL;

    err = redisGetReply( c, (void**)&reply );

    if (err == EREDIS_OK) {
      /* Good */
      /* Previous to clean? */
      _eredis_r_free_reply( r );
      /* New reply */
      r->reply = reply;
    } else {
      /* Bad */
      if (reply)
        freeReplyObject( reply );

      err = c->err;
    }

    return reply;
}

/**
 * @brief eredis reader reply
 *
 * @param r     eredis reader
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_reply( eredis_reader_t *r )
{
  redisContext *c;
  eredis_reply_t *reply;
  int retry, err;

  if (r->cmds_replied >= r->cmds_nb) {
    fprintf(stderr,
            "eredis: api misuse: all cmds are already replied: %d/%d\n",
            r->cmds_replied, r->cmds_nb);
    return NULL;
  }

  /* Retry allowed if already connected */
  retry = (r->ctx) ? r->e->reader_retry : 0;

  do {
    reply   = NULL;

    if (_eredis_r_send( r, &c ) == EREDIS_ERR)
      break;

    err = redisGetReply( c, (void**)&reply );

    if (err == EREDIS_OK) {
      /* Good */
      /* Pipelining - we consider all cmds are well requested */
      r->cmds_requested = r->cmds_nb;
      /* Previous to clean? */
      _eredis_r_free_reply( r );
      /* New reply */
      r->reply = reply;
      /* Mark it as replied */
      r->cmds_replied ++;
      break;
    }

    /* Bad */
    if (reply)
      freeReplyObject( reply );

    err = c->err;
    _eredis_r_ctx(r, 1);

    /* retry? */
    if (err != REDIS_ERR_IO && err != REDIS_ERR_EOF)
      break;

  } while ( retry -- >0 );

  return reply;
}

/**
 * @brief reader subscribe and read
 *  Need at least one appended SUBSCRIBE/PSUBSCRIBED cmd
 *
 * @param r     eredis reader
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_subscribe( eredis_reader_t *r )
{
  redisContext *c;
  eredis_reply_t *reply;
  int retry, err;

  if (r->cmds_nb == 0) {
    fprintf(stderr,
            "eredis: api misuse: subscribe need "
            "at least one appended command\n");
    return NULL;
  }

  /* Retry allowed if already connected */
  retry = (r->ctx) ? r->e->reader_retry : 0;

  do {
    reply   = NULL;

    if (_eredis_r_send( r, &c ) == EREDIS_ERR)
      break;

    /* Replies of subscribe commands if there is some */
    if (r->cmds_replied < r->cmds_nb) {
      do {
        err = redisGetReply( c, (void**)&reply );
        if (err != EREDIS_OK)
          goto reconnect;

        if (reply) {
          freeReplyObject( reply );
          reply = NULL;
        }

        r->cmds_replied ++;
      } while (r->cmds_replied < r->cmds_nb);

      r->cmds_requested = r->cmds_replied = r->cmds_nb;
    }

    /* Message from subscribed channel */
    err = redisGetReply( c, (void**)&reply );

    if (err == EREDIS_OK) {
      /* Good */
      /* Previous to clean? */
      _eredis_r_free_reply( r );
      /* New reply */
      r->reply = reply;
      break;
    }

reconnect:
    /* Bad */
    if (reply) {
      freeReplyObject( reply );
      reply = NULL;
    }

    err = c->err;
    _eredis_r_ctx(r, 1);

    /* Reset cmds_requested to ensure re-subscribe */
    r->cmds_requested = r->cmds_replied = 0;

    /* retry? */
    if (err != REDIS_ERR_IO && err != REDIS_ERR_EOF)
      break;

  } while ( retry -- >0 );

  return reply;
}

/**
 * @brief detach current reply from reader context
 *
 * By default, because it is covering the most frequent usage, Eredis
 * manage itself the cleanup of the reply.
 * This function allows to detach from the reader context the latest
 * reply returned by any eredis_r_X function.
 *
 * This allows to continue using the reply while issuing a new cmd
 * to the reader.
 * This reply will need to be free via eredis_reply_free.
 *
 * @param   r eredis reader
 *
 * @return  the reply (same as returned by previous eredis_r_X call)
 */
  eredis_reply_t *
eredis_r_reply_detach( eredis_reader_t *r )
{
  eredis_reply_t *reply;

  reply     = r->reply;
  r->reply  = NULL;

  return reply;
}

/**
 * @brief eredis reader vargs command
 *
 * @param r       eredis reader
 * @param format  format
 * @param ap      length of command
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_vcmd( eredis_reader_t *r, const char *format, va_list ap )
{
  if (eredis_r_append_vcmd( r, format, ap ) != EREDIS_OK)
    return NULL;

  return eredis_r_reply( r );
}

/**
 * @brief eredis reader 'printf' style command
 *
 * @param r       eredis reader
 * @param format  format
 * @param ...     list
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_cmd( eredis_reader_t *r, const char *format, ... )
{
  eredis_reply_t *reply;
  va_list ap;

  va_start(ap,format);
  reply = eredis_r_vcmd( r, format, ap );
  va_end(ap);

  return reply;
}

/**
 * @brief eredis reader argc/argv command
 *
 * @param r       eredis reader
 * @param argc    argument count
 * @param argv    argument vector
 * @param argvlen argument length vector
 *
 * @return reply (redisReply)
 */
  eredis_reply_t *
eredis_r_cmdargv( eredis_reader_t *r,
                  int argc, const char **argv, const size_t *argvlen )
{
  if (eredis_r_append_cmdargv( r, argc, argv, argvlen ) != EREDIS_OK)
    return NULL;

  return eredis_r_reply( r );
}

