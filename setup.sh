#!/usr/bin/bash

WD=$(pwd)
PIN_PATH=~/pin
$PIN_PATH/pin -h
if [ $? -neq 0 ]
    then
        echo "Install pin and update PIN_PATH in setup.sh. See README.md for link."
        exit 1
fi
cd $PIN_PATH/source/tools/ManualExamples
make pinatrace.test TARGET=intel64
../../../pin -t obj-intel64/pinatrace.so -- /bin/ls -l
mv pinatrace.out $WD/ls-l.txt
cd $WD
clang -v
if [ $? -neq 0 ]
    then
        echo "clang is not installed, but can be installed with"
        echo "apt-get install clang"
        echo "Or using a package manager of your choice"
        exit 1
fi
mkdir build

./build.sh
./cache ls-l.txt
