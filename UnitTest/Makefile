BIN=$(HOME)/bin
UTS= 001 002 003 004 005 006

.PHONY:	test
test: $(UTS:%=%.log)

.PRECIOUS:	%.tx
%.tx: % $(BIN)/prep
	THRESH=1 $(BIN)/prep $<

%.log: %.tx
	echo "Doing $@" > $@
	diff  $*._tx $*.tx >> $@
	diff  $*._no $*.no >> $@
	diff  $*._vr $*.vr >> $@

.PHONY:	ci
ci:
	ci -u Makefile Chron $(UTS) $(UTS:%=%._no) $(UTS:%=%._tx) $(UTS:%=%._vr)
