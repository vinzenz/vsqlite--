#!/bin/sh
set -euo pipefail

DIST_VERSION="$(cat VERSION)"
DIST_NAME='vsqlite++'
DIST_FULL="${DIST_NAME}-${DIST_VERSION}"

echo "Generating ${DIST_FULL} archives..."

git clean -d -f

git archive --format=tar --prefix="${DIST_FULL}/" HEAD | gzip -9 > "${DIST_FULL}.tar.gz"
git archive --format=tar --prefix="${DIST_FULL}/" HEAD | bzip2 -9 > "${DIST_FULL}.tar.bz2"
git archive --format=tar --prefix="${DIST_FULL}/" HEAD | xz -9 -e > "${DIST_FULL}.tar.xz"
git archive --format=zip --prefix="${DIST_FULL}/" HEAD > "${DIST_FULL}.zip"

DIST_TARBALLS="${DIST_FULL}.tar.xz ${DIST_FULL}.tar.gz ${DIST_FULL}.zip ${DIST_FULL}.tar.bz2"

md5sum $DIST_TARBALLS > "${DIST_FULL}.md5sum.txt"
sha1sum $DIST_TARBALLS > "${DIST_FULL}.sha1sum.txt"
sha256sum $DIST_TARBALLS > "${DIST_FULL}.sha256sum.txt"
sha512sum $DIST_TARBALLS > "${DIST_FULL}.sha512sum.txt"
