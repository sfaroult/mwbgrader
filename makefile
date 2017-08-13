CFLAGS=-I/usr/local/opt/libxml2/include/libxml2 \
	   -I./include -Wall
OBJ=obj/mwbkey.o obj/dbop.o obj/synth.o obj/catpath.o obj/kwsearch.o \
	obj/schema.o obj/grad.o obj/grading.o obj/levenshtein.o obj/debug.o
LIBS=-lxml2 -lsqlite3

all: mwbgrader scanxml scansetup

mwbgrader: obj/mwbgrader.o obj/miniz.o obj/scanxml.o $(OBJ)
	gcc -o $@ obj/mwbgrader.o obj/miniz.o obj/scanxml.o $(OBJ) $(LIBS)

obj/mwbgrader.o: src/mwbgrader.c
	gcc -c $(CFLAGS) -o $@ $<

obj/miniz.o: src/miniz.c
	gcc -c $(CFLAGS) -o $@ $<

obj/debug.o: src/debug.c
	gcc -c $(CFLAGS) -o $@ $<

scanxml: obj/scanxml_sa.o $(OBJ)
	gcc -o $@ obj/scanxml_sa.o $(OBJ) $(LIBS)

scansetup: obj/scansetup.o $(OBJ)
	gcc -o $@ obj/scansetup.o $(OBJ) $(LIBS)

obj/scanxml_sa.o: src/scanxml.c include/mwbkey.h include/synth.h \
   	include/catpath.h
	gcc -c $(CFLAGS) -D STANDALONE -o $@ $<

obj/scanxml.o: src/scanxml.c include/mwbkey.h include/synth.h \
   	include/catpath.h
	gcc -c $(CFLAGS) -o $@ $<

obj/scansetup.o: src/scanxml.c
	gcc -c -D SETUP -D STANDALONE $(CFLAGS) -o $@ $<

obj/mwbkey.o: src/mwbkey.c
	gcc -c $(CFLAGS) -o $@ $<

obj/synth.o: src/synth.c
	gcc -c $(CFLAGS) -o $@ $<

obj/catpath.o: src/catpath.c
	gcc -c $(CFLAGS) -o $@ $<

obj/grading.o: src/grading.c include/grad.h
	gcc -c $(CFLAGS) -o $@ $<

obj/grad.o: src/grad.c
	gcc -c $(CFLAGS) -o $@ $<

obj/kwsearch.o: src/kwsearch.c
	gcc -c $(CFLAGS) -o $@ $<

obj/schema.o: src/schema.c
	gcc -c $(CFLAGS) -o $@ $<

obj/dbop.o: src/dbop.c
	gcc -c $(CFLAGS) -o $@ $<

obj/levenshtein.o: src/levenshtein.c
	gcc -c $(CFLAGS) -o $@ $<

clean:
	-rm mwbgrader scanxml scansetup
	-rm obj/*
