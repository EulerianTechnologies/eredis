#!/bin/sh
#
# This is a simple shell script to run the tests for Eredis. It launches
# several local Redis instances for Eredis to connect to, runs the given
# Eredis test executable, then stops the Redis server and cleans up any
# temporary files that may have been written during the test (whether or
# not it succeeded or failed). It is designed to be run by CTest (the
# CMake test runner) with the "make test" command.
#

set -e

# Keep track of the command that this script was run with. It may be
# helpful for running it again later with GDB or Valgrind.
FULL_COMMAND="$0 $@"

while [ $# -gt 0 ]; do
  case $1 in
    -g|--gdb)
      if [ -z "$2" ] || echo $2 | egrep -qs '^\-'; then
        GDB_EXECUTABLE="$(which gdb 2>/dev/null)"
        if [ -z "$GDB_EXECUTABLE" ]; then
          echo "GDB not found. Please specify it with \"$1 GDB_COMMAND\"." 1>&2
          exit 1
        fi
      else
        GDB_EXECUTABLE="$2"
        shift
      fi
      ;;

    -n|--no-cleanup)
      CLEANUP_TEST_FILES=0
      ;;

    -r|--redis)
      if [ -z "$2" ] || echo $2 | egrep -qs '^\-'; then
        echo "Missing Redis server executable." 1>&2
        exit 1
      fi
      REDIS_EXECUTABLE="$2"
      shift
      ;;

    -t|--test)
      if [ -z "$2" ] || echo $2 | egrep -qs '^\-'; then
        echo "Missing Eredis test executable." 1>&2
        exit 1
      fi
      TEST_EXECUTABLE="$2"
      shift
      ;;

    -v|--valgrind)
      if [ -z "$2" ] || echo $2 | egrep -qs '^\-'; then
        VALGRIND_EXECUTABLE="$(which valgrind 2>/dev/null)"
        if [ -z "$VALGRIND_EXECUTABLE" ]; then
          echo "Valgrind not found. Please specify it with \"$1 VALGRIND_COMMAND\"." 1>&2
          exit 1
        fi
      else
        VALGRIND_EXECUTABLE="$2"
        shift
      fi
      ;;

    --help)
      echo "usage: $0 [-g|--gdb GDB_COMMAND] [-v|--valgrind VALGRIND_COMMAND] [-n|--no-cleanup] -r|--redis REDIS_EXECUTABLE -t|--test TEST_EXECUTABLE"
      exit 0
      ;;

    *)
      echo "Invalid Argument: $1" 1>&2
      exit 1
      ;;
  esac
  shift
done

if [ -z "$REDIS_EXECUTABLE" ]; then
  echo "Missing REDIS_EXECUTABLE. See '$0 --help'." 1>&2
  exit 1
fi
if [ -z "$TEST_EXECUTABLE" ]; then
  echo "Missing TEST_EXECUTABLE. See '$0 --help'." 1>&2
  exit 1
fi
if ! echo $CLEANUP_TEST_FILES | egrep -xqs '[0-9]+'; then
  CLEANUP_TEST_FILES=1
fi

if [ -z "$GDB_EXECUTABLE" ]; then
  TEST_EXECUTABLE_NAME="$(basename "$TEST_EXECUTABLE")"
  if [ -z "$TEST_EXECUTABLE_NAME" ]; then
    echo "Failed to get the basename of ${TEST_EXECUTABLE}." 1>&2
    exit 1
  fi

  TEST_TIMEOUT_SEC=30
  TEST_TIMEOUT_TIME="$(($(date +%s 2>/dev/null) + $TEST_TIMEOUT_SEC))"
  if ! echo $TEST_TIMEOUT_TIME | egrep -xqs '[0-9]+'; then
    echo "Failed to set the test timeout to $TEST_TIMEOUT_SEC seconds." 1>&2
    exit 1
  fi
fi

EREDIS_HOST_FILE="$PWD/test-hosts-${TEST_EXECUTABLE_NAME}.conf"

cleanup() {
  exit_code=$?

  # Dump the Redis databases (for debugging) if the test failed.
  if [ $exit_code -ne 0 ] && [ -r "$EREDIS_HOST_FILE" ]; then
    REDIS_CLI="$(dirname $REDIS_EXECUTABLE)/redis-cli"
    if [ -x "$REDIS_CLI" ]; then
      while IFS='' read -r line; do
        if echo "$line" | egrep -xqs '[^:\s]+:[0-9]+'; then
          host="$(echo $line | cut -d: -f1)"
          port="$(echo $line | cut -d: -f2)"
          echo "Dumping Redis ${host}:${port}"
          "$REDIS_CLI" -h "$host" -p "$port" KEYS '*' || true
        elif echo "$line" | egrep -xqs '\S+'; then
          unixsocket="$line"
          echo "Dumping Redis $unixsocket"
          "$REDIS_CLI" -s "$unixsocket" KEYS '*' || true
        fi
      done < "$EREDIS_HOST_FILE"
    fi
  fi

  for pidfile in redis-*.pid; do
    pid="$(egrep -so '[0-9]+' $pidfile | head -n 1)"
    if [ -n "$pid" ]; then
      echo "Killing the Redis server with PID $pid"
      kill $pid
      slept=0
      while ps -eo pid | awk '{print $1}' | grep -xqs $pid; do
        sleep 1
        slept=$((slept+1))
        if [ $slept -ge 10 ]; then
          echo "Failed to kill Redis $pid after $slept seconds"
          kill -9 $pid
        fi
      done
    fi
  done

  echo "Cleaning up temporary configuration files"
  rm -f "$EREDIS_HOST_FILE" redis-*.pid

  if [ $CLEANUP_TEST_FILES -gt 0 ]; then
    echo "Cleaning up log files"
    rm -f redis-*.log
  fi
}

trap cleanup 0 INT QUIT TERM TSTP

# Start the Redis server instances for this test, and write the Eredis
# configuration file describing where to find them.
rm -f "$EREDIS_HOST_FILE"
for portspec in \
  "$PWD/redis-local.sock::" \
  ":localhost:9144" \
  ":127.0.0.1:9145" \
  ":127.0.0.1:9146"
do
  unixsocket="$(echo $portspec | cut -d: -f1)"
  bind="$(echo $portspec | cut -d: -f2)"
  port="$(echo $portspec | cut -d: -f3)"

  if [ -n "$unixsocket" ]; then
    echo "Starting Redis $unixsocket"
    "$REDIS_EXECUTABLE" \
      --unixsocket "$unixsocket" \
      --unixsocketperm 700 \
      --daemonize yes \
      --pidfile "$PWD/$(basename "$unixsocket").pid" \
      --logfile "$PWD/$(basename "$unixsocket").log" \
      --appendonly no \
      --save ""
    echo "$unixsocket" >> "$EREDIS_HOST_FILE"
  elif [ -n "$bind" ] && [ -n "$port" ]; then
    echo "Starting Redis ${bind}:${port}"
    "$REDIS_EXECUTABLE" \
      --bind $bind \
      --port $port \
      --daemonize yes \
      --pidfile "$PWD/redis-${port}.pid" \
      --logfile "$PWD/redis-${port}.log" \
      --appendonly no \
      --save ""
    echo "${bind}:${port}" >> "$EREDIS_HOST_FILE"
  else
    echo "ERROR! Invalid portspec." 1>&2
    exit 1
  fi
done

# All of our test programs but one take an Eredis host file as an argument.
# The eredis-drop-noexpire test takes a Redis host instead. We should probably
# find a better way to do this, but for now, detect that test specifically and
# set the command line arguments for the test appropriately.
if [ "$(basename "$TEST_EXECUTABLE")" = 'eredis-drop-noexpire' ]; then
  TEST_EXECUTABLE_ARGS="127.0.0.1:9145"
else
  TEST_EXECUTABLE_ARGS="$EREDIS_HOST_FILE"
fi

# Run the test.
if [ -n "$GDB_EXECUTABLE" ]; then
  echo "Running $TEST_EXECUTABLE under $GDB_EXECUTABLE"
  echo "> run $TEST_EXECUTABLE_ARGS"
  "$GDB_EXECUTABLE" "$TEST_EXECUTABLE"
elif [ -n "$VALGRIND_EXECUTABLE" ]; then
  echo "Running $TEST_EXECUTABLE under $VALGRIND_EXECUTABLE"
  "$VALGRIND_EXECUTABLE" --leak-check=full "$TEST_EXECUTABLE" "$TEST_EXECUTABLE_ARGS"
else
  echo "Running $TEST_EXECUTABLE"
  "$TEST_EXECUTABLE" "$TEST_EXECUTABLE_ARGS" &
  test_pid=$!
  if [ -z "$test_pid" ]; then
    echo "ERROR! Failed to get the PID of ${TEST_EXECUTABLE_NAME}." 1>&2
    killall -9 "$TEST_EXECUTABLE_NAME"
    exit 1
  fi

  # Don't allow the test to run for more than 60 seconds.
  #
  # NOTE: CTest will kill the test if it takes too long, but it sends it
  # SIGKILL, not SIGTERM, so our cleanup handler doesn't get a chance to
  # run. Timeout ourselves, and cleanup properly, before CTest intervenes.
  #
  while ps -eo pid | awk '{print $1}' | grep -xqs $test_pid; do
    test_time="$(date +%s 2>/dev/null || echo 0)"
    if [ $test_time -gt $TEST_TIMEOUT_TIME ]; then
      echo "TIMEOUT! The test may not take longer than $TEST_TIMEOUT_SEC seconds." 1>&2
      echo "Command: $FULL_COMMAND" 1>&2
      killall -9 "$TEST_EXECUTABLE_NAME"
      exit 1
    fi
    sleep 1
  done

  wait $test_pid
  test_ret=$?
  if [ $test_ret -ne 0 ]; then
    echo "ERROR! $TEST_EXECUTABLE_NAME $test_pid returned $test_ret" 1>&2
    echo "Command: $FULL_COMMAND" 1>&2
  fi
  exit $test_ret
fi
