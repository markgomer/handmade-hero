#!/bin/bash

# disable echoing

mkdir -p ./build

# set debug flags

# build in this directory
pushd build/
# -g and -Wall are debug symbols
gcc -g -Wall ../src/main.c -o main
# go back to initial directory
popd
