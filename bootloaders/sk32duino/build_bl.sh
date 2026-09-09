#!/bin/bash
# Convenience build script for the sk32duino bootloader.
# Forces the minimal-RT chconf options and builds in place.
set -e
cd "$(dirname "$0")"

# Minimal-RT kernel sweep (see cfg/chconf.h).
for m in REGISTRY WAITEXIT SEMAPHORES CONDVARS CONDVARS_TIMEOUT \
         EVENTS EVENTS_TIMEOUT DYNAMIC MAILBOXES MEMCORE MEMPOOLS \
         OBJ_CACHES JOBS; do
  sed -i -E "s/^(#define CH_CFG_USE_${m}[[:space:]]+)TRUE/\1FALSE/" cfg/chconf.h
done

echo "---- effective chconf switches ----"
grep -E "#define CH_CFG_USE_(REGISTRY|WAITEXIT|SEMAPHORES|CONDVARS|EVENTS|DYNAMIC|MAILBOXES|MEMCORE|MEMPOOLS|OBJ_CACHES|JOBS) " cfg/chconf.h

echo "---- building ----"
make -j8 "$@"
