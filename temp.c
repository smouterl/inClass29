/****************************************
 * Filename: ICA29.c
 * Description: three-process pipeline with buffers for variable timing
 * Based on Bob Turney's shared-memory and semaphore example
 * Compile: gcc -Wall -Wextra -o ICA29 ICA29.c -pthread -lm
 * Run:     ./ICA29          (random timing: 1 second +/- 10%)
 *          ./ICA29 burst    (also simulate bursts of fast/slow work)
 ****************************************/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define BUFFER_SIZE 20
#define NUM_ITEMS 5

typedef struct {
    double values[BUFFER_SIZE];
    int read_index;
    int write_index;
    sem_t empty;
    sem_t filled;
} Buffer;

typedef struct {
    Buffer buffer1;               // square -> add ten
    Buffer buffer2;               // add ten -> square root
} SharedMemory;

void add_ten(double *x) {
    *x = *x + 10.0;
}

void square(double *x) {
    *x = (*x * *x);
}

void square_root(double *x) {
    *x = sqrt(*x);
}

/* Each buffer has one producer and one consumer. Only the producer changes
 * write_index, and only the consumer changes read_index. The semaphores keep
 * the producer from overwriting an unread value and the consumer from reading
 * an empty slot. */
void wait_for(sem_t *sem) {
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }
    }
}

void put(Buffer *buffer, double value) {
    wait_for(&buffer->empty);
    buffer->values[buffer->write_index] = value;
    buffer->write_index = (buffer->write_index + 1) % BUFFER_SIZE;
    sem_post(&buffer->filled);
}

double get(Buffer *buffer) {
    double value;
    wait_for(&buffer->filled);
    value = buffer->values[buffer->read_index];
    buffer->read_index = (buffer->read_index + 1) % BUFFER_SIZE;
    sem_post(&buffer->empty);
    return value;
}

/* Simulate work outside the buffer so other stages can run concurrently. */
void work_delay(int stage, int item, int burst) {
    long milliseconds = 900 + rand() % 201;
    struct timespec delay;

    if (burst && item >= 5 && item < 15) {
        if (stage == 1) milliseconds = 300;  // produce a burst
        if (stage == 2) milliseconds = 1800; // temporarily slow consumer
    }
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) { }
}

int main(int argc, char *argv[]) {
    char shm_name[64];
    int shm_fd;
    SharedMemory *shm_addr;
    pid_t pid1, pid2;
    int burst = 0;
    int status1, status2;

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "burst") != 0)) {
        fprintf(stderr, "Usage: %s [burst]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) burst = 1;
    //setvbuf(stdout, NULL, _IONBF, 0);

    /* A unique name avoids interfering with another run of this program. */
    snprintf(shm_name, sizeof(shm_name), "/ica29_%ld", (long)getpid());
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    if (ftruncate(shm_fd, sizeof(SharedMemory)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(shm_name);
        return EXIT_FAILURE;
    }
    shm_addr = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE,
                    MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    shm_unlink(shm_name); // mapping remains available to both children
    if (shm_addr == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    memset(shm_addr, 0, sizeof(*shm_addr));
    /* pshared = 1: these semaphores are shared between processes. */
    if (sem_init(&shm_addr->buffer1.empty, 1, BUFFER_SIZE) == -1 ||
        sem_init(&shm_addr->buffer1.filled, 1, 0) == -1 ||
        sem_init(&shm_addr->buffer2.empty, 1, BUFFER_SIZE) == -1 ||
        sem_init(&shm_addr->buffer2.filled, 1, 0) == -1) {
        perror("sem_init");
        munmap(shm_addr, sizeof(*shm_addr));
        return EXIT_FAILURE;
    }

    pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (pid1 == 0) {
        srand((unsigned int)(time(NULL) ^ getpid()));
        for (int i = 0; i < NUM_ITEMS; i++) {
            double value = i + 1;
            work_delay(1, i, burst);
            square(&value);
            put(&shm_addr->buffer1, value);
            printf("Square: item %d -> %.2f\n", i + 1, value);
        }
        munmap(shm_addr, sizeof(*shm_addr));
        return EXIT_SUCCESS;
    }

    pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        kill(pid1, SIGTERM);
        waitpid(pid1, NULL, 0);
        return EXIT_FAILURE;
    }
    if (pid2 == 0) {
        srand((unsigned int)(time(NULL) ^ getpid()));
        for (int i = 0; i < NUM_ITEMS; i++) {
            double value = get(&shm_addr->buffer1);
            work_delay(2, i, burst);
            add_ten(&value);
            put(&shm_addr->buffer2, value);
            printf("Add 10: item %d -> %.2f\n", i + 1, value);
        }
        munmap(shm_addr, sizeof(*shm_addr));
        return EXIT_SUCCESS;
    }

    /* Parent is the third stage. Every process handles the same item count. */
    srand((unsigned int)(time(NULL) ^ getpid()));
    for (int i = 0; i < NUM_ITEMS; i++) {
        double value = get(&shm_addr->buffer2);
        work_delay(3, i, burst);
        square_root(&value);
        printf("Result: item %d -> %.4f\n", i + 1, value);
    }

    if (waitpid(pid1, &status1, 0) == -1 ||
        waitpid(pid2, &status2, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    sem_destroy(&shm_addr->buffer1.empty);
    sem_destroy(&shm_addr->buffer1.filled);
    sem_destroy(&shm_addr->buffer2.empty);
    sem_destroy(&shm_addr->buffer2.filled);
    munmap(shm_addr, sizeof(*shm_addr));

    return (WIFEXITED(status1) && WEXITSTATUS(status1) == 0 &&
            WIFEXITED(status2) && WEXITSTATUS(status2) == 0)
               ? EXIT_SUCCESS : EXIT_FAILURE;
}
 