CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDLIBS = -pthread -lrt -lm

ICA29: temp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

.PHONY: clean
clean:
	$(RM) ICA29
