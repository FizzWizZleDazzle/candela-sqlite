#!/bin/sh
# Builds the sqlite shared library this package ships, into dist/.
#
# The package vendors the sqlite amalgamation and adds a thin shim that turns
# sqlite's pointer API into the integer handles candela's foreign interface
# can carry. One build per desktop target; the release workflow runs this on
# each runner and packs the result.
set -eu
cd "$(dirname "$0")"
mkdir -p dist

FLAGS="-O2 -fvisibility=hidden \
  -DSQLITE_THREADSAFE=0 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_OMIT_DEPRECATED \
  -DSQLITE_DEFAULT_MEMSTATUS=0 \
  -DSQLITE_DQS=0"
SOURCES="vendor/sqlite3.c shim/candela_sqlite.c"

case "$(uname -s)" in
  Linux*)
    # shellcheck disable=SC2086
    cc $FLAGS -fPIC -shared -o dist/sqlite.so $SOURCES -lm
    ;;
  Darwin*)
    # shellcheck disable=SC2086
    clang $FLAGS -dynamiclib -o dist/sqlite.dylib $SOURCES
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows*)
    # shellcheck disable=SC2086
    clang $FLAGS -shared -fuse-ld=lld -o dist/sqlite.dll $SOURCES
    ;;
  *)
    echo "build-native.sh: unsupported system $(uname -s)" >&2
    exit 1
    ;;
esac
ls -l dist
