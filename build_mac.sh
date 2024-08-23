#!/usr/bin/env sh

clang -o brut -std=c99 -I./src -I./src/luajit/src -L./src/luajit/src -lluajit -Wall -Werror -fpic main.c
