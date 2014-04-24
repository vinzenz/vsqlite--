#!/bin/sh

DIST_VERSION=`cat VERSION`
DIST_NAME='vsqlite++'
DIST_FULL=$DIST_NAME-$DIST_VERSION
echo "Generating $DIST_FULL..."

git clean -d -f
#git archive tags/$DIST_VERSION --format=tar --prefix="$DIST_FULL/" | bzip2 > $DIST_FULL.tar.bz2
#git archive tags/$DIST_VERSION --format=tar --prefix="$DIST_FULL/" | gzip > $DIST_FULL.tar.gz
#git archive tags/$DIST_VERSION --format=tar --prefix="$DIST_FULL/" | xz -c > $DIST_FULL.tar.xz
#git archive tags/$DIST_VERSION --format=zip --prefix="$DIST_FULL/" > $DIST_FULL.zip
./autogen.sh
./configure --prefix=/usr
make dist-gzip
make dist-bzip2
make dist-xz
make dist-zip

DIST_TARBALLS="$DIST_FULL.tar.xz $DIST_FULL.tar.gz $DIST_FULL.zip $DIST_FULL.tar.bz2"

md5sum $DIST_TARBALLS > $DIST_FULL.md5sum.txt
sha1sum $DIST_TARBALLS > $DIST_FULL.sha1sum.txt
sha256sum $DIST_TARBALLS > $DIST_FULL.sha256sum.txt
sha512sum $DIST_TARBALLS > $DIST_FULL.sha512sum.txt

