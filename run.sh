#!/bin/sh

gcc main.c -ggdb3 -lm $(pkg-config --cflags --libs pappl) -o escp-printer-app && exec ./escp-printer-app "$@"
