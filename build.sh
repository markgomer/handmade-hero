#!/bin/bash

# disable echoing

mkdir -p ./build

# set debug flags

# build in this directory
pushd build/
# -g and -Wall are debug symbols
g++ -g -Wall ../src/main.cpp -o main
# go back to initial directory
popd
