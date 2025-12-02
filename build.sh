#!/bin/bash

# disable echoing

mkdir -p ./build

# set debug flags

# build with something
pushd build/
gcc ../src/main.c -o main
popd
