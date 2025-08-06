#
# tinyflex: A minimal, dependency-free, single-header library, FLEX encoder.
# Written by Davidson Francis (aka Theldus) - 2025.
#
# This is free and unencumbered software released into the public domain.
#

CC     ?= cc
CFLAGS  = -Wall -Wextra -std=c89
CFLAGS += -I$(CURDIR)/..

.PHONY: all clean
all: encode_file send_ttgo send_ttgo_at

encode_file.o: encode_file.c $(CURDIR)/../tinyflex.h
send_ttgo.o:   send_ttgo.c   $(CURDIR)/../tinyflex.h
send_ttgo_at.o:   send_ttgo_at.cpp   $(CURDIR)/../tinyflex.h

encode_file: encode_file.o
send_ttgo:   send_ttgo.o
send_ttgo_at:   send_ttgo_at.o

clean:
	rm -f *.o
	rm -f encode_file
	rm -f send_ttgo
	rm -f send_ttgo_at
