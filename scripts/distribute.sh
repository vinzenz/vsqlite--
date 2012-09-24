#!/bin/sh

echo "Creating autotools environment"
autoreconf --install --force --verbose
echo "Running configure..."
./configure --prefix=/usr > /dev/null
echo "Done."
echo "Making distribution packages..."
make dist-gzip dist-bzip2 dist-xz dist-zip
echo "Moving files one level below"
mv vsqlite++*.tar.* ..
mv vsqlite++*.zip ..
echo "Running cleanup"
git clean -d -f
