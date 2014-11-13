all: Makefile tirprog.c
	gcc -g -o tirprog tirprog.c

clean:
	rm tirprog
