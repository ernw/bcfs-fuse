OBJECTS=bcfs_fuse.o bcfs.o
CFLAGS=-Wall -D_GNU_SOURCE -g -fPIC
LDFLAGS=-fPIC
FUSE_CFLAGS=$(shell pkg-config fuse --cflags)
FUSE_LIBS=$(shell pkg-config fuse --libs)

bcfs_fuse: ${OBJECTS}
	gcc ${FUSE_LIBS} $^ -o $@ ${LDFLAGS}

bcfs_fuse.o: bcfs_fuse.c bcfs.h
	gcc ${FUSE_CFLAGS} ${CFLAGS} -c $< -o $@

bcfs.o: bcfs.c bcfs.h
	gcc ${CFLAGS} -c $< -o $@

clean:
	rm -f *.o bcfs_fuse

.PHONY: clean
