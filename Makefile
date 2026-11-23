CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDLIBS = -pthread -lrt

ICA29: ICA29.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

.PHONY: clean
clean:
	$(RM) ICA29
