#!/bin/bash
#
# Run cpid tests that require a redis instance.
#

set -e

# XXX hard-coded port could lead to conflicts...
PORT=44445
CONF=$(mktemp)
echo "port $PORT" >> "$CONF"
echo "bind 127.0.0.1" >> "$CONF"
echo "loglevel warning" >> "$CONF"
echo "notify-keyspace-events Ex$" >> "$CONF"
# This is just to surpress warnings on our devfairs
echo "tcp-backlog 128" >> "$CONF"
redis-server "$CONF" &
sleep 0.1

trap cleanup HUP INT TERM EXIT
cleanup()
{
  pkill -9 -P $$
}

T="./build/test/test_training"
for t in $($T -list_tests "[.redis]"); do
  echo "FLUSHALL" | redis-cli -p "$PORT" >/dev/null
  $T "$t" -redis_host 127.0.0.1 -redis_port "$PORT" "$@"
done
