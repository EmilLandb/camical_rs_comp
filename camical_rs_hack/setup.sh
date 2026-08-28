#!/bin/sh

(cd aiger && ./configure && make)
(cd cadical && ./configure && make -j)
(cd camical && ./configure.sh && make)
