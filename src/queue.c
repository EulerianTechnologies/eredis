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
 * @file queue.c
 * @brief ERedis queues (writer and reader double linked lists)
 * @author Guillaume Fougnies <guillaume@eulerian.com>
 * @version 0.1
 * @date 2016-03-29
 */

/*
 * Write Queue cmd
 */
  static inline void
_eredis_wqueue_push( eredis_t *e, char *s, int l )
{
  wqueue_ent_t *ent;

  ent = malloc(sizeof(wqueue_ent_t));
  ent->s = s;
  ent->l = l;
  ent->prev = ent->next = ent;

  pthread_mutex_lock( &e->async_lock );

  if (e->wqueue.fst) {
    ent->next   = e->wqueue.fst;
    ent->prev   = ent->next->prev;
    ent->next->prev = ent->prev->next = ent;
  }
  else {
    e->wqueue.fst = ent;
  }

  e->wqueue.nb ++;

  pthread_mutex_unlock( &e->async_lock );
}

  static inline void
_eredis_wqueue_unshift( eredis_t *e, char *s, int l )
{
  wqueue_ent_t *ent;

  ent = malloc(sizeof(wqueue_ent_t));
  ent->s = s;
  ent->l = l;
  ent->prev = ent->next = ent;

  pthread_mutex_lock( &e->async_lock );

  if (e->wqueue.fst) {
    ent->next   = e->wqueue.fst;
    ent->prev   = ent->next->prev;
    ent->next->prev = ent->prev->next = ent;
  }
  e->wqueue.fst = ent;

  e->wqueue.nb ++;

  pthread_mutex_unlock( &e->async_lock );
}

  static inline void
_eredis_ent_rm_inlock( eredis_t *e, wqueue_ent_t *ent )
{
  if (e->wqueue.fst == ent) {
    /* one element */
    if (e->wqueue.nb == 1) e->wqueue.fst = NULL;
    else                  e->wqueue.fst = ent->next;
  }
  else if (ent->next == ent)
    /* Self pointing element, not in a list and not the first element */
    return;

  ent->next->prev = ent->prev;
  ent->prev->next = ent->next;

  ent->next = ent->prev = ent;

  e->wqueue.nb --;
}

  static inline char *
_eredis_wqueue_shift( eredis_t *e, int *pl )
{
  wqueue_ent_t *ent = NULL;
  char *s = NULL;

  if (pl)
    *pl = 0;

  if (! e->wqueue.nb)
    return NULL;

  pthread_mutex_lock( &e->async_lock );

  if (e->wqueue.nb && (ent = e->wqueue.fst))
    _eredis_ent_rm_inlock( e, ent );

  pthread_mutex_unlock( &e->async_lock );

  if (ent) {
    if (pl)
      *pl = ent->l;
    s = ent->s;
    free( ent );
  }

  return s;
}

/*
 * Reader Queue
 */
  static inline void
_eredis_reader_rm_inlock( eredis_t *e, eredis_reader_t *r )
{
  if (e->rqueue.fst == r) {
    /* one element */
    if (e->rqueue.nb == 1)  e->rqueue.fst = NULL;
    else                    e->rqueue.fst = r->next;
  }
  else if (r->next == r)
    /* Self pointing element, not in a list and not the first element */
    return;

  r->next->prev = r->prev;
  r->prev->next = r->next;

  r->next = r->prev = r;

  e->rqueue.nb --;
}

  static inline void
_eredis_reader_touch_inlock( eredis_t *e, eredis_reader_t *r )
{
  if (e->rqueue.fst != r) {
    _eredis_reader_rm_inlock( e, r );

    if (e->rqueue.fst) {
      r->next   = e->rqueue.fst;
      r->prev   = r->next->prev;
      r->next->prev = r->prev->next = r;
    }
    e->rqueue.fst = r;
    e->rqueue.nb ++;
  }
}

  static inline void
_eredis_reader_push_inlock( eredis_t *e, eredis_reader_t *r )
{
  if (e->rqueue.fst) {
    r->next   = e->rqueue.fst;
    r->prev   = r->next->prev;
    r->next->prev = r->prev->next = r;
  }
  else {
    e->rqueue.fst = r;
  }

  e->rqueue.nb ++;
}

  static inline void
_eredis_reader_untouch_inlock( eredis_t *e, eredis_reader_t *r )
{
  if (r == e->rqueue.fst) {
    /* Fast move to queue end */
    e->rqueue.fst = e->rqueue.fst->next;
  }
  else {
    _eredis_reader_rm_inlock( e, r );
    _eredis_reader_push_inlock( e, r );
  }
}

/*
 * Shift one
 */
  static inline eredis_reader_t *
_eredis_rqueue_shift( eredis_t *e )
{
  eredis_reader_t *r;

  pthread_mutex_lock( &e->reader_lock );

  r = e->rqueue.fst;

  if (r)
    _eredis_reader_rm_inlock( e, r );

  pthread_mutex_unlock( &e->reader_lock );

  return r;
}

/*
 * Release a reader to the queue
 */
  static inline void
_eredis_rqueue_release( eredis_reader_t *r )
{
  eredis_t *e = r->e;

  pthread_mutex_lock( &e->reader_lock );

  _eredis_reader_touch_inlock( e, r );
  r->free  = 1;

  pthread_cond_signal( &e->reader_cond );

  pthread_mutex_unlock( &e->reader_lock );
}

/*
 * Get a reader from the queue
 */
  static inline eredis_reader_t *
_eredis_rqueue_get( eredis_t *e )
{
  eredis_reader_t *r = NULL;

  pthread_mutex_lock( &e->reader_lock );

  if (e->rqueue.fst && e->rqueue.fst->free) {
    r = e->rqueue.fst;
    goto unlock;
  }

  if (e->rqueue.nb >= e->reader_max) {
    while (e->rqueue.fst->free == 0)
      pthread_cond_wait( &e->reader_cond, &e->reader_lock );
    r = e->rqueue.fst;
    goto unlock;
  }

  r = _eredis_reader_new( e );
  if (! r)
    return NULL;

unlock:
  if (r) {
    r->free = 0;
    _eredis_reader_untouch_inlock( e, r );
  }

  pthread_mutex_unlock( &e->reader_lock );

  return r;
}

