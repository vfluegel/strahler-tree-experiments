CC=gcc

all: gstree 

gstree: genstree.c
	$(CC) -std=c23 genstree.c -o gstree -g

clean:
	rm gstree
