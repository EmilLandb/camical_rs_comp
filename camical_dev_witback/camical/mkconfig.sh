#!/bin/sh
VERSION="`cat VERSION`"
GITID="5f82dd0680793863809275b155d07a0a526ef850"
COMPILE="`sed -e '/^COMPILE=/!d' -e 's,COMPILE=,,' makefile`"
LC_TIME="en_US"
export LC_TIME
DATE="`date 2>/dev/null|sed -e 's,  *, ,g'`"
OS="`uname -srmn 2>/dev/null`"
BUILD="`echo $DATE $OS|sed -e 's,^ *,,' -e 's, *$,,'`"
cat <<EOF
#define VERSION "$VERSION"
#define GITID "$GITID"
#define COMPILE "$COMPILE"
#define BUILD "$BUILD"
EOF
