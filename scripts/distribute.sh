#!/bin/sh

echo "Creating autotools environment"
autoreconf --install --force --verbose
echo "Running configure..."
./configure --prefix=/usr > /dev/null
echo "Done."
echo "Making distribution packages..."
make dist-clean
make dist-gzip 
make dist-bzip2 
make dist-xz 
make dist-zip
echo "Moving files one level below"
mv vsqlite++*.tar.* ..
mv vsqlite++*.zip ..
echo "Running cleanup"
git clean -d -f
