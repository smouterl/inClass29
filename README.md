# Shared-buffer timing demo

This program demonstrates a bounded, thread-safe FIFO shared by a producer and
a consumer. The buffer holds up to 20 items; when it is full, the producer waits
for space instead of dropping items. The consumer begins after a 3-second
initial latency, then removes or reports an empty result once per second.

The producer timing mode is selectable:

- `random`: item intervals are uniformly selected from 0.9 to 1.1 seconds.
- `burst`: intervals alternate between 0.5 seconds for 10 items and 1.5 seconds
  for the next 10 items.

## Build and run

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -pthread ICA29.c -lm -o buffer_demo
./buffer_demo random 30
./buffer_demo burst 30
```

The duration is in seconds and defaults to 30. Each consumer result includes
elapsed time and the remaining buffer occupancy.
