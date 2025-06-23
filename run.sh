#!/bin/sh

CFLAGS="-I/usr/include/p11-kit-1 -D_REENTRANT -I/usr/include/libpng16 -I/usr/include/libusb-1.0"
LIBS="pappl/pappl/libpappl.a -lcups -lm -lpam -lavahi-common -lavahi-client -ljpeg -lpng16 -lusb-1.0 -ludev -lssl -lcrypto -ldl -lz -pthread"

make -C pappl &&
gcc main.c $CFLAGS $LIBS -o escp-printer-app && exec ./escp-printer-app "$@"
