#!/usr/bin/bash

WD=$(pwd)
PIN_PATH=~/pin
cd $PIN_PATH/source/tools/ManualExamples
make pinatrace.test TARGET=intel64
../../../pin -t obj-intel64/pinatrace.so -- /bin/ls -l
mv pinatrace.out $WD/ls-l.txt
cd $WD

./build.sh
./cache ls-l.txt
