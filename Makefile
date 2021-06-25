CFLAGS = -g -Wall -pthread
CFLAGS_2 = -g -Wall

targets: srnode dvnode

srnode: srnode.o
	gcc $(CFLAGS) srnode.o -o srnode

srnode.o: srnode.c
	gcc $(CFLAGS) -c srnode.c

dvnode: dvnode.o
	gcc $(CFLAGS_2) dvnode.o -o dvnode

dvnode.o: dvnode.c
	gcc $(CFLAGS_2) -c dvnode.c

.PHONY: clean
clean:
	rm -f *.o *~ a.out core srnode dvnode