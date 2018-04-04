#ifndef EREDIS_H
#define EREDIS_H
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
 * @file eredis.h
 * @brief eredis is a fast redis client over hiredis managing
 *        mirrored async writers via libev,
 *        multiple thread-safe sync readers and auto-reconnect.
 *
 * @author Guillaume Fougnies <guillaume@eulerian.com>
 * @version 0.1
 * @date 2016-03-30
 */

#include <eredis-hiredis.h>

#ifdef __cplusplus
extern "C" {
#endif
  /*
   * Documentation (doxygen) in C files
   */

#ifndef _EREDIS_C
  typedef struct eredis_s eredis_t;
  typedef struct eredis_reader_s eredis_reader_t;
#endif
  typedef struct redisReply eredis_reply_t;

#define EREDIS_ERRCMD   -2
#define EREDIS_ERR      -1
#define EREDIS_OK        0

  /* New */
  eredis_t * eredis_new( void );
  /* Free */
  void eredis_free( eredis_t *e );
  /* Shutdown */
  void eredis_shutdown( eredis_t *e );
  /* Add a host */
  int eredis_host_add( eredis_t *e, char *target, int port );
  /* Add hosts via configuration file (one host per line) */
  int eredis_host_file( eredis_t *e, const char *file );

  /* Set timeout */
  void eredis_timeout( eredis_t *e, int timeout_ms );
  /* Set max readers */
  void eredis_r_max( eredis_t *e, int max );
  /* Set retry for reader */
  void eredis_r_retry( eredis_t *e, int retry );

  /* Set connect command */
  int eredis_pc_cmd( eredis_t *e, const char *fmt, ... );

  /* Run async */
  int eredis_run( eredis_t *e );
  int eredis_run_thr( eredis_t *e );

  /* Add write command */
  int eredis_w_fcmd( eredis_t *e, const char *cmd, size_t len );
  int eredis_w_vcmd( eredis_t *e, const char *fmt, va_list ap );
  int eredis_w_cmd( eredis_t *e, const char *fmt, ... );
  int eredis_w_cmdargv(
    eredis_t *e, int argc, const char **argv, const size_t *argvlen );
  /* Pending commands */
  int eredis_w_pending( eredis_t *e );

  /* Reader */
  eredis_reader_t * eredis_r( eredis_t *e );
  void eredis_r_release( eredis_reader_t *reader );
  /* force all append and cmd to clear (without releasing the reader) */
  void eredis_r_clear( eredis_reader_t *reader );
  /* Get another reply (pipelining) */
  eredis_reply_t * eredis_r_reply( eredis_reader_t *reader );

  /* Get subscribe reply */
  eredis_reply_t * eredis_r_subscribe( eredis_reader_t *reader );

  int eredis_r_append_fcmd(
    eredis_reader_t *reader, const char *cmd, size_t len );
  int eredis_r_append_vcmd(
    eredis_reader_t *reader, const char *format, va_list ap );
  int eredis_r_append_cmd( eredis_reader_t *reader, const char *format, ... );
  int eredis_r_append_cmdargv(
    eredis_reader_t *reader,
    int argc, const char **argv, const size_t *argvlen);

  /* Detach reply from reader - will need to be free manually */
  eredis_reply_t * eredis_r_reply_detach( eredis_reader_t *reader );

  /* Make an 'append' and a 'reply'. Return the reply of the first CMD proceed */
  eredis_reply_t *
    eredis_r_vcmd( eredis_reader_t *reader, const char *format, va_list ap );
  eredis_reply_t *
    eredis_r_cmd( eredis_reader_t *reader, const char *format, ... );
  eredis_reply_t *
    eredis_r_cmdargv( eredis_reader_t *reader,
                      int argc, const char **argv, const size_t *argvlen );

  /* Utils */
  void eredis_reply_dump( eredis_reply_t *reply );
  void eredis_reply_free( eredis_reply_t *reply );

#ifdef __cplusplus
}
#endif

#if (EREDIS_ERR != REDIS_ERR)
#error "Wrong hiredis mapping EREDIS_ERR != REDIS_ERR"
#endif


#endif /* EREDIS_H */
