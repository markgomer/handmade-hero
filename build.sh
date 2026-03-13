#!/usr/bin/env bash

mkdir -p ./build

# build in this directory
pushd build/

# -g: debugging symbols
# -Wall: all warning messages
# prefix-map is for debugging inside the container:
# -fdebug-prefix-map=/var/home=/run/host/var/home \
g++ -g -Wall -O0 -std=c++23 \
-DHANDMADE_SLOW=1 \
-DHANDMADE_LINUX=1 \
-DHANDMADE_INTERNAL=1 \
../src/linux_game.cpp -o game \
-lX11 -lasound

# go back to initial directory
popd
