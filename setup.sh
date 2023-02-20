#!/usr/bin/bash

PIN_PATH=~/pin # EDIT THIS IF NECESSARY

if ! clang -v
then
        echo "clang is not installed, but can be installed with"
        echo "apt-get install clang"
        echo "Or using a package manager of your choice"
        exit 1
fi


if ! cmake --version
then
        echo "cmake is not installed, but can be installed with"
        echo "apt-get install cmake"
        echo "Or using a package manager of your choice"
        exit 1
fi

if ! python --version
then
        echo "python is not installed, but can be installed with"
        echo "apt-get install python"
        echo "Or using a package manager of your choice"
        exit 1
fi

WD=$(pwd)

if ! $PIN_PATH/pin -h
then
        echo "Install pin and update PIN_PATH in setup.sh. See README.md for link."
        exit 1
fi
cd $PIN_PATH/source/tools/ManualExamples
make pinatrace.test TARGET=intel64
../../../pin -t obj-intel64/pinatrace.so -- /bin/ls -l
mv pinatrace.out $WD/ls-l.trace
cd $WD

./build.py -c -b Release
cd ./build
make


if ./cache ../ls-l.trace
then
        echo "Setup was successful. ls-l.trace was written to $WD"
        echo "cache was simulated ont this trace and printed results"
        echo "$WD/build was created and executables can be found there"
fi
