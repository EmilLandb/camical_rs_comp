#!/bin/sh

(cd aiger && ./configure && make)
(cd cadical && ./configure CXXFLAGS=-std=c++17 && make -j)
(cd camical && ./configure.sh && make)
