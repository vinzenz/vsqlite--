#!/bin/sh

python scons/scons.py -c
find ./ -name "*.pyc" -exec rm -f {} \;
rm config.log
rm -rf .scon*
