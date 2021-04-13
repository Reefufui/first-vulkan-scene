#!/bin/bash

cd shaders
sh compile_shaders.sh
cd ..

if [ -d "build" ]; then
    cd build &&
    make -j 5 &&
    cd ..
else
    mkdir build &&
    cd build &&
    cmake .. &&
    make -j 5 &&
    cd ..
fi
