CFLAGS=-g -Wall #-pg
CFLAGS+=-O2
LDFLAGS=-lm #-pg

BIN=$(HOME)/bin

all::	$(BIN)/prep

$(BIN)/prep:	prep
	ln -f prep $(BIN)/

prep:	prep.o

prep.o:	prep.h

.PHONY: ut
ut:
	(cd UnitTest; make)

.PHONY:	ci
ci:
	ci -u Makefile prep.c prep.h
