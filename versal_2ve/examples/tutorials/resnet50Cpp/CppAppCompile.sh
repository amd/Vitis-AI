#!/usr/bin/bash

$CXX -I$SDKTARGETSYSROOT/usr/include -I$SDKTARGETSYSROOT/usr/include/onnxruntime/core/session -O2 -pipe -g -feliminate-unused-debug-types -o input.o -c ./input.cpp

$CXX -O2 -pipe -g -feliminate-unused-debug-types  -Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed  -Wl,-z,relro,-z,now -rdynamic "input.o" -o model-app.elf -L$SDKTARGETSYSROOT/usr/lib -Wl,-rpath,$SDKTARGETSYSROOT/usr/lib -lonnxruntime

