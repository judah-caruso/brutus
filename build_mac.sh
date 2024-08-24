#!/usr/bin/env sh

clang -o brutus -std=c99 -I./src -I./src/luajit/src -L./src/luajit/src -lluajit -Wall -Werror -fpic -flto main.c
