#!/bin/bash

git submodule foreach "git fetch && git reset --hard origin/main"\
 && cd utils\
 && make clean\
 && make\
 && make memcheck
