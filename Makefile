CC = cc
CFLAGS = -O2 -Wall -Wextra -std=c11

all: sender receiver

sender: sender.c
	$(CC) $(CFLAGS) -o sender sender.c

receiver: receiver.c
	$(CC) $(CFLAGS) -o receiver receiver.c

clean:
	rm -f sender receiver

.PHONY: all clean
