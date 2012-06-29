#!/bin/bash

gcc -fPIC -g -c -Wall cJSON.c
gcc -shared -Wl,-soname,libcJSON.so.1 -o libcJSON.so.1.0.1 cJSON.o -lc

