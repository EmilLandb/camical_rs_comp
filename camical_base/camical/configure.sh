#!/bin/sh
#--------------------------------------------------------------------------#
debug=no
check=unknown
logging=unknown
static=no
riss=unknown
cryptominisat=unknown
pthread=no
trans=no
ipasir=no
zlib=no
#--------------------------------------------------------------------------#
msg () {
  echo "[configure.sh] $*"
}
die () {
  echo "configure.sh: error: $*" 1>&1
  exit 1
}
#--------------------------------------------------------------------------#
usage () {
cat <<EOF
usage: configure.sh [ <option> ... ]

where <option> is one of the following

-h                  print this command line option summary
-g                  compile with debugging symbols
-c                  compile with assertion checking (default for '-g')
-l                  compile with logging code (default for '-g')

-s | --static       force static compilation

--ipasir=<lib>      link to this ipasir library

--trans             include disfunctional transition relation code

--no-riss           do not to search and compile against RISS
--no-cryptominisat  do not to search and compile against CryptoMiniSAT
EOF
}
while [ $# -gt 0 ]
do
  case $1 in
    -h) usage; exit 0;;
    -g) debug=yes;;
    -c) check=yes;;
    -s|--static) static=yes;;
    -l) logging=yes;;
    --ipasir=*)
      ipasirlib="`expr $1 : '^--ipasir=\(.*\)$'`"
      [ -f "$ipasirlib" ] || \
      die "can not find IPASIR library '$ipasirlib'"
      ipasir=yes
      ;;
    --trans) trans=yes;;
    --no-riss) riss=no;;
    --no-cryptominisat) cryptominisat=no;;
    *) die "invalid option '$1' (try '-h')";;
  esac
  shift
done
#--------------------------------------------------------------------------#
[ -f ../cadical/build/libcadical.a ] || \
die "could not find CaDiCaL library '../cadical/build/libcadical.a'"
[ -f ../cadical/src/cadical.hpp ] || \
die "could not find CaDiCaL header '../cadical/src/cadical.hpp'"
[ -f ../aiger/aiger.o ] || \
die "could not find AIGER library '../aiger/aiger.o'"
[ -f ../aiger/aiger.h ] || \
die "could not find AIGER header '../aiger/aiger.h'"
#--------------------------------------------------------------------------#
if [ $ipasir = yes ]
then
  DEPOBJS="$DEPOBJS $ipasirlib"
  msg "using static IPASIR library '$ipasirlib'"
fi
if [ $cryptominisat = unknown ]
then
  cryptominisat=no
  for cmsprefix in /usr/local
  do
    cmsversion=5
    if [ -f $cmsprefix/include/cryptominisat$cmsversion/cryptominisat.h ]
    then
      if [ $static = yes -a \
	   -f $cmsprefix/lib/libcryptominisat$cmsversion.a ]
      then
	msg "found static CryptoMiniSAT Version $cmsversion in '$cmsprefix'"
	DEPHDRS="$DEPHDRS $cmsprefix/include/cryptominisat$cmsversion/cryptominisat.h"
	DEPOBJS="$DEPOBJS $cmsprefix/lib/libcryptominisat$cmsversion.a"
	cryptominisat=yes
	pthread=yes
	zlib=yes
	break
      elif [ $static = no -a \
             -f $cmsprefix/lib/libcryptominisat$cmsversion.so ]
      then
	msg "found dynamic CryptoMiniSAT Version $cmsversion in '$cmsprefix'"
	DEPHDRS="$DEPHDRS $cmsprefix/include/cryptominisat$cmsversion/cryptominisat.h"
	cryptominisat=yes
	break
      fi
    fi
  done
fi
if [ $riss = unknown ]
then
  riss=no
  for rissprefix in /usr/local
  do
    if [ -f $rissprefix/lib/libriss.a -a \
         -f $rissprefix/lib/libcoprocessor.a -a \
         -f $rissprefix/include/riss/librissc.h ]
    then
      msg "found static RISS in '$rissprefix'"
      DEPHDRS="$DEPHDRS $rissprefix/include/riss/librissc.h"
      DEPOBJS="$DEPOBJS $rissprefix/lib/libriss.a $rissprefix/lib/libcoprocessor.a"
      pthread=yes
      riss=yes
      zlib=yes
      break
    fi
  done
fi
#--------------------------------------------------------------------------#
[ $check = unknown ] && check="$debug"
[ $logging = unknown ] && logging="$debug"
#--------------------------------------------------------------------------#

COMPILE="g++ -Wall -Wextra"

# First ordinary flags.
#
if [ $debug = yes ]
then
  COMPILE="$COMPILE -g3"
else
  COMPILE="$COMPILE -O3"
fi
[ $pthread = yes ] && COMPILE="$COMPILE -pthread"
[ $static = yes ] && COMPILE="$COMPILE -static"

# Macro definitions.
#
[ $check = no ] && COMPILE="$COMPILE -DNDEBUG"
[ $logging = yes ] && COMPILE="$COMPILE -DLOGGING"
[ $ipasir = yes ] && COMPILE="$COMPILE -DIPASIR"
[ $cryptominisat = yes ] && COMPILE="$COMPILE -DCRYPTOMINISAT"
[ $riss = yes ] && COMPILE="$COMPILE -DRISS"
[ $trans = yes ] && COMPILE="$COMPILE -DTRANS"

#--------------------------------------------------------------------------#
# Collect source include directories in 'INCDIRS'.
#
INCDIRS=" -I../cadical/src -I../aiger"
[ $cryptominisat = yes ] && INCDIRS="$INCDIRS -I$cmsprefix/include/cryptominisat$cmsversion"
[ $riss = yes ] && INCDIRS="$INCDIRS -I$rissprefix/include/riss"

#--------------------------------------------------------------------------#
DEPHDRS="$DEPHDRS ../cadical/src/cadical.hpp"
DEPOBJS="$DEPOBJS ../cadical/build/libcadical.a"
#--------------------------------------------------------------------------#
# Collect library include directories in 'LIBDIRS'.
#
DEFAULTLIBDIRS="-L/lib -L/usr/lib -L/usr/local/lib"
LIBDIRS=" -L../cadical/build"
addlibdir() {
  found=no
  for dir in $LIBDIRS $DEFAULTLIBDIRS
  do
    [ "$dir" = "-L$1" ] || continue
    found=yes
    break
  done
  [ $found = yes ] && return
  LIBDIRS="$LIBDIRS -L$1"
}
[ $ipasir = yes ] && addlibdir "`dirname $ipasirlib`"
[ $cryptominisat = yes ] && addlibdir "$cmsprefix/lib"
[ $riss = yes ] && addlibdir "$rissprefix/lib"
for dir in $libdirs none
do
  [ "$dir" = none ] && break
  COMPILE="$COMPILE -L$dir"
done

#--------------------------------------------------------------------------#
# Collect actual libraries in 'LIBS'.
#
LIBS=" ../aiger/aiger.o"
[ $ipasir = yes ] && \
LIBS="$LIBS -l`basename $ipasirlib .a | sed -e 's,\<lib,,'`"
[ $cryptominisat = yes ] && LIBS="$LIBS -lcryptominisat$cmsversion"
[ $riss = yes ] && \
LIBS="$LIBS -Wl,--start-group -lriss -lcoprocessor -Wl,--end-group"
LIBS="$LIBS -lcadical"

#--------------------------------------------------------------------------#
# Add further required libraries to 'LIBS'.
#
[ $cryptominisat = yes ] && LIBS="$LIBS -lm4ri"
[ $zlib = yes ] && LIBS="$LIBS -lz"

#--------------------------------------------------------------------------#
echo "[configure.sh] $COMPILE$INCDIRS$LIBDIRS$LIBS"
sed \
  -e "s,@COMPILE@,$COMPILE," \
  -e "s,@DEPHDRS@,$DEPHDRS," \
  -e "s,@DEPOBJS@,$DEPOBJS," \
  -e "s,@INCDIRS@,$INCDIRS," \
  -e "s,@LIBDIRS@,$LIBDIRS," \
  -e "s#@LIBS@#$LIBS#" \
makefile.in > makefile
