#!/usr/bin/bash
cd src
MAKEFILE=Makefile_release
if [ $1 = "debug" ]; then
    MAKEFILE=Makefile_debug
fi
mv $MAKEFILE Makefile
make
mv cache ..
mv Makefile $MAKEFILE