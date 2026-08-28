#!/bin/sh
camical --optimize=1 $1 73
optimize=$?
echo optimize=1 $optimize
camical --optimize=0 $1 73
nooptimize=$?
echo optimize=0 $nooptimize
exec test $optimize = $nooptimize
