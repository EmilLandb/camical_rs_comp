#!/bin/sh
[ -f makefile ] && make clean
GITID="`git show|awk '{print $2;exit}'`"
SHORTGITID="`echo $GITID|sed -e 's,^\(......\).*,\1,'`"
VERSION="`cat VERSION`"
name=camical-$VERSION-$SHORTGITID
dir=/tmp/$name
archive=$name.tar.xz
rm -rf $dir
mkdir $dir || exit 1
cp -Rp * $dir/
rm -rf $dir/test
sed -i -e "s,^GITID=.*,GITID=\"$GITID\"," $dir/mkconfig.sh
sed -i \
-e '/^test:/d' \
-e '/make -C test/d' \
-e 's, test , ,' \
$dir/makefile.in
sed -i -e 's,make test,make,' $dir/README
cd /tmp
rm -f $archive
tar cfJ $archive $name
ls -l /tmp/$archive
