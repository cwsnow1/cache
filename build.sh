#!/usr/bin/bash
cd src
MAKEFILE=Makefile_release
if [ $1 = "-c" ]; then
    echo "Clean build"
    rm ../build/*
elif [ $1 = "debug" ]; then
    echo "Clean build"
    rm ../build/*
    MAKEFILE=Makefile_debug
fi
mv $MAKEFILE Makefile
make
mv *.o ../build
mv cache ..
mv Makefile $MAKEFILE