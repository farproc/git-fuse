
default: all

all: ./git-fuse

./git-fuse: ./src/git-fuse.c
	$(CC) `pkg-config osxfuse,libgit2 --cflags --libs` -Wall ./src/git-fuse.c -o ./git-fuse

clean:
	rm -f ./git-fuse
