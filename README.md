[![Build Status](https://travis-ci.org/EulerianTechnologies/eredis.svg?branch=master)](https://travis-ci.org/EulerianTechnologies/eredis)

# EREDIS

Eredis is a C client library built over Hiredis.
It is lightweight, high performance, reentrant and thread-safe.  
It aims to provide features for real production environment and keep it simple.

For write commands (SET, HSET, EXPIRE, ...):
* Async commands via asynchronous events (libev)
* Async commands mirroring across multiple redis servers (shared-nothing)

For any commands:
* Thread-safe high speed pool of blocking connections: efficient persistent connections
* Automatic and seamless fail-over and reconnect

Integration:
* Embedded Hiredis ensures the full Redis protocol support
* Reply structure is the Hiredis efficient redisReply
* Eredis allows you to switch from a single host mode to a an efficient
shared-nothing/fail-over mode by just activating a new host.


The integrated master-slave mechanism in Redis works ok
for a small number of nodes or workload, but it could kill a
master node bandwidth in high load.
The many-to-many mechanism permits a more efficient and reliable
workload by obviously needing exactly the same bandwidth on each node.


## Compile

```shell
git clone ...
cd eredis
cmake .
make
# optional
make doc
make install
```

## 

## Usage

Each function is described in the C code (doxygen).  
The raw list is available in the unique header file: [eredis.h](/include/eredis.h "eredis.h").

### init
```c
#include <eredis.h>

eredis_t *e = eredis_new();

/* Set timeout - default 5000ms */
eredis_timeout( e, 200 );

/* Set max readers - default 10 */
eredis_r_max( e, 50 );

/* Set retry for reader - default 1 */
eredis_r_retry( e, 1 );
```

### add redis targets
The first host provided becomes the "prefered" one.  
The readers will always reconnect to this one in case of a down/up event.  
```c
/* manually */
eredis_host_add( e, "/var/run/eredis.sock", 0 );
eredis_host_add( e, "host2", 6379 );
eredis_host_add( e, "host3", 6379 );

/* via configuration file - one line per host:port */
eredis_host_file( e, "my-hosts.conf" );
```

### add post-connection requests (beta)
For 'AUTH' or any one-time command needed to be executed after connect.
Eredis ensures that these commands are executed in order before any other
reader or writer commands.
```c
char *pwd = "mysecret";
eredis_pc_cmd( e, "AUTH mysecret" );
eredis_pc_cmd( e, "AUTH %s", pwd );
eredis_pc_cmd( e, "SCRIPT DEBUG YES" );
```

### launch the async loop
Mandatory for using async writes or auto-reconnection to the "prefered" host.
```c
/* in its own thread - blocking call */
eredis_run( e );

/* in a eredis managed thread - non-blocking call */
eredis_run_thr( e );
```

### sync requests (to get reply)
```c
eredis_reader_t *r;
eredis_reply_t *reply; /* aka redisReply from hiredis */

/* get a reader */
r = eredis_r( e );

/* Add one request (pipelining) */
eredis_r_append_cmd( reader, "GET key1");

/* Add one request and process,
   return key1 reply (the first one from queue) */
reply = eredis_r_cmd( reader, "GET key2");

if ( I_NEED_ALL_REPLIES ) {
 /* current 'reply' hosts 'key1' reply */

 /* Get key2 reply */
 reply = eredis_r_reply( reader );
 ...
}

if ( I_NEED_TO_SEND_MORE ) {
 /* this one fetches all possible replies left in the pipelining */
 eredis_r_clear( reader );

 /* reader is ready to perform a new batch of r_cmd+replies */
 ..
}

/* Release the reader */
eredis_r_release( r );
```

### subscribe requests (beta, blocking)
```c
eredis_reader_t *r;
eredis_reply_t *reply; /* aka redisReply from hiredis */

/* get a reader */
r = eredis_r( e );

/* Add one request */
eredis_r_append_cmd( reader, "SUBSCRIBE chan1");

/* Get subscribe requests.
   Replies from SUBSCRIBE commands are omitted.
   Eredis manages reconnect and re-subscribe to channels */

while (( reply = eredis_r_subscribe( reader ) ))
{
 ...
 if ( HAVE_ENOUGH )
  break;
}

/* Add a new channel */
eredis_r_append_cmd( reader, "SUBSCRIBE chan2");

while (( reply = eredis_r_subscribe( reader ) ))
{
 ...
 if ( HAVE_ENOUGH )
  break;
}

/* 'release' is not disconnecting, just unsubscribe properly */
eredis_r_cmd( reader, "UNSUBSCRIBE" );

/* Release the reader */
eredis_r_release( r );
```

### async requests (no reply, non-blocking)
```c
eredis_w_cmd( e, "SET key1 10" );
```

### stop
```c
/* Exit the event loop (from any thread) */
eredis_shutdown( e );

/* Stop the event loop and threadif needed,
   close connections and release memory */
eredis_free( e );
```

## Day-to-day

When a Redis server goes down, Eredis async loop will detect it and
retry to reconnect every second HOST_DISCONNECTED_RETRIES (10) times (= 10 seconds).   
After this, it will retry once every HOST_FAILED_RETRY_AFTER (20) seconds.

To avoid data loss, if all specified Redis server are down, Eredis will
keep in memory the last unsent QUEUE_MAX_UNSHIFT (10000) commands.

If a Redis server goes down and up, it could be needed to resynchronize
with an active node. The master-slave mechanism is perfect for that.  
In redis.conf, add 'slave-read-only no'.  
After the Redis server start, make a "redis-cli SLAVEOF hostX".  
Once 'redis-cli info Replication' pops a 'master_sync_in_progress:0', it's done.  
make a "redis-cli SLAVEOF no one" and it comes back to a 'master' status.  
This process can easily be scripted.


## AUTHOR

Eredis is used on nearly 500 servers at Eulerian Technologies.  
It is written and maintained by Guillaume Fougnies (guillaume at
eulerian dot com).  
Redis and Hiredis are great!  
We hope you will find Eredis great.  
It is released under the BSD license.  
