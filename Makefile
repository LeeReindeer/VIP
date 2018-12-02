CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -O3

all: vip

debug:
	$(CC) $(CFLAGS) vip.c -g -o vipd

re: 
	make clean;make

clean:
	rm -f vip vipd