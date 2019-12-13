#!/bin/bash
make clean
make CILKSCALE=1
mv screensaver .screensaver.cilkscale
make clean
make
awsrun-plot cilkscale ./screensaver 500 input/koch.in
mv log.awsrun/*.svg ~/www/$1
