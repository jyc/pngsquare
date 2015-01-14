TARGET=pngsquare
CC=gcc
CFLAGS=-O2 -pipe -std=c99 -pedantic -Wall
INCLUDES=
LFLAGS=
LIBS=-lm -lfreeimage
SOURCES_DIR=src
OBJECTS=main.o heap.o

.PHONY: all dep clean

all: $(TARGET)

# generate using `make dep`
include Makefile.dep

%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(LFLAGS) $(INCLUDES) $(LIBS) -o $@

dep: 
	$(CC) -MM $(SOURCES_DIR)/*.c > Makefile.dep

clean:
	rm -rf *.o
	rm -rf $(TARGET)
