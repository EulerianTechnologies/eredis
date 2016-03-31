[![Build Status](https://travis-ci.org/EulerianTechnologies/eredis.svg?branch=master)](https://travis-ci.org/EulerianTechnologies/eredis)

# EREDIS

Eredis is a C client library built over Hiredis.
It is lightweight, high performance, reentrant and thread-safe.  
It aims to provide basic features needed in real production environment.

* Async commands (like writes) via asynchronous events (libev)
* Manage async commands mirroring across multiple redis servers (shared-nothing)
* Thread-safe pool of readers via sync
* Automatic and seamless fail-over and reconnect.

Embedded Hiredis ensures the full Redis protocol support.  
Reply's structure is from Hiredis.  
Multiple hosts is not mandatory to use it.

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

### init
```c
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
/* Add one request and process - got key1 reply */
reply = eredis_r_cmd( reader, "GET key2");
/* key2 reply - not mandatory to get all replies */
reply = eredis_r_reply( reader );
...

/* release a reader */
r = eredis_r_release( e );
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

Eredis provides a shared-nothing mechanism.  
The integrated master-slave mechanism provided in Redis works ok
for a small number of nodes or workload, but it could kill a
master node bandwidth in high load.  
The many-to-many mechanism permits a more efficient and reliable
workload.

When a Redis server goes down, Eredis async loop will detect it and
retry to reconnect every second during HOST_DISCONNECTED_RETRIES (10 seconds).  
After this, it will retry one time every HOST_FAILED_RETRY_AFTER (20 seconds).

To avoid data loss, if all specified Redis server are down, Eredis will
keep in memory the last unsent QUEUE_MAX_UNSHIFT (10000) commands.

If a Redis server goes down and up, it could be needed to resynchronize
with an active node. The master-slave mechanism is perfect for that.  
In redis.conf, add 'slave-read-only no'.  
After the Redis server start, make a "SLAVEOF hostX" and monitor the
resync status. Once finished, make a "SLAVEOF no one" and it's done.


## AUTHOR

Eredis is used on nearly 500 servers at Eulerian Technologies.  
It is written and maintained by Guillaume Fougnies (guillaume at
eulerian dot com).  
Redis and Hiredis are great!
We hope you will find Eredis great.  
It is released under the BSD license.  
