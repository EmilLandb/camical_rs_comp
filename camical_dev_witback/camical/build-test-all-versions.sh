#!/bin/sh
die () {
  echo "build-test-all-versions.sh: error: $*" 1>&2
  exit 1
}
run () {
  echo "./configure.sh $*"
  ./configure.sh $* 1>/dev/null 2>/dev/null
  [ $? = 0 ] || die "configuration failed run './configure.sh $*'"
  make 1>/dev/null 2>/dev/null || die "build failed run 'make'"
  make test 1>/dev/null 2>/dev/null || die "tests failed run 'make test'"
}
for c in " --no-cryptominisat" ""
do
for r in " --no-riss" ""
do
for s in "" " -s"
do
for g in " -g" "" " -c" " -l" " -c -l"
do
run $c$r$s$g
done
done
done
done
