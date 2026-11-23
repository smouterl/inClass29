/****************************************
 * Filename: multiproc3.c
 * Description: ICA29 #5 - three-process pipeline using
 *              shared memory, multiple buffering, and
 *              random/burst processing delays.
 * Based on starter code by Bob Turney.
 * Compile: gcc -Wall -Wextra -o multiproc3 multiproc3.c -lm -pthread
 ****************************************/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#define SHM_NAME "/my_shm"
#define SEM_EMPTY1 "/my_empty1"
#define SEM_FULL1  "/my_full1"
#define SEM_MUTEX1 "/my_mutex1"
#define SEM_EMPTY2 "/my_empty2"
#define SEM_FULL2  "/my_full2"
#define SEM_MUTEX2 "/my_mutex2"

#define BUFFER_SIZE 20
#define NUM_VALUES 6  // 0 through 5, as on the whiteboard

typedef struct {
    double buffer1[BUFFER_SIZE];
    double buffer2[BUFFER_SIZE];
    int head1;
    int tail1;
    int head2;
    int tail2;
} SharedMemory;

static void add_ten(double *x) {
    *x = *x + 10.0;
}

static void square(double *x) {
    *x = *x * *x;
}

static void square_root(double *x) {
    *x = sqrt(*x);
}

// Retry if a signal interrupts sem_wait().
static void wait_sem(sem_t *sem) {
    int result;
    do {
        result = sem_wait(sem);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("sem_wait");
        _exit(EXIT_FAILURE);
    }
}

static void post_sem(sem_t *sem) {
    if (sem_post(sem) == -1) {
        perror("sem_post");
        _exit(EXIT_FAILURE);
    }
}

// Add one item to a circular buffer.
static void write_buffer(double buffer[], int *tail, double value,
                         sem_t *empty, sem_t *full, sem_t *mutex) {
    wait_sem(empty);  // Wait until there is room.
    wait_sem(mutex);  // Lock the buffer.

    buffer[*tail] = value;
    *tail = (*tail + 1) % BUFFER_SIZE;

    post_sem(mutex);  // Unlock the buffer.
    post_sem(full);   // Announce a new item.
}

// Remove one item from a circular buffer.
static double read_buffer(double buffer[], int *head,
                          sem_t *empty, sem_t *full, sem_t *mutex) {
    wait_sem(full);   // Wait until an item is ready.
    wait_sem(mutex);  // Lock the buffer.

    double value = buffer[*head];
    *head = (*head + 1) % BUFFER_SIZE;

    post_sem(mutex);  // Unlock the buffer.
    post_sem(empty);  // Announce a free slot.
    return value;
}

// Most operations take about 1 second (+/-10%).
// Occasionally a short or long burst changes the processing time.
static void random_delay(void) {
    int choice = rand() % 10;
    long milliseconds;

    if (choice == 0) {
        milliseconds = 100 + rand() % 201;   // Fast burst: 0.1-0.3 s
    } else if (choice == 1) {
        milliseconds = 1600 + rand() % 601;  // Slow burst: 1.6-2.2 s
    } else {
        milliseconds = 900 + rand() % 201;   // Normal: 0.9-1.1 s
    }

    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
        // Keep sleeping for the remaining duration.
    }
}

static sem_t *create_sem(const char *name, unsigned int initial_value) {
    sem_t *sem = sem_open(name, O_CREAT | O_EXCL, 0600, initial_value);
    if (sem == SEM_FAILED) {
        perror(name);
        exit(EXIT_FAILURE);
    }
    return sem;
}

int main(void) {
    int shm_fd;
    SharedMemory *shm_addr;

    // Remove names left behind by a previous run.
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY1);
    sem_unlink(SEM_FULL1);
    sem_unlink(SEM_MUTEX1);
    sem_unlink(SEM_EMPTY2);
    sem_unlink(SEM_FULL2);
    sem_unlink(SEM_MUTEX2);

    // Create and map shared memory BEFORE forking.
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    if (ftruncate(shm_fd, sizeof(SharedMemory)) == -1) {
        perror("ftruncate");
        return EXIT_FAILURE;
    }

    shm_addr = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE,
                    MAP_SHARED, shm_fd, 0);
    if (shm_addr == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    memset(shm_addr, 0, sizeof(SharedMemory));

    // Each buffer starts with 20 empty slots and 0 full slots.
    sem_t *empty1 = create_sem(SEM_EMPTY1, BUFFER_SIZE);
    sem_t *full1  = create_sem(SEM_FULL1, 0);
    sem_t *mutex1 = create_sem(SEM_MUTEX1, 1);
    sem_t *empty2 = create_sem(SEM_EMPTY2, BUFFER_SIZE);
    sem_t *full2  = create_sem(SEM_FULL2, 0);
    sem_t *mutex2 = create_sem(SEM_MUTEX2, 1);

    // Child 1: Process 2 reads Buffer 1, adds 10, writes Buffer 2.
    pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("fork process 2");
        return EXIT_FAILURE;
    }
    if (pid2 == 0) {
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
        for (int i = 0; i < NUM_VALUES; i++) {
            double x = read_buffer(shm_addr->buffer1, &shm_addr->head1,
                                   empty1, full1, mutex1);
            add_ten(&x);
            random_delay();
            write_buffer(shm_addr->buffer2, &shm_addr->tail2, x,
                         empty2, full2, mutex2);
        }
        _exit(EXIT_SUCCESS);
    }

    // Child 2: Process 3 reads Buffer 2 and takes the square root.
    pid_t pid3 = fork();
    if (pid3 == -1) {
        perror("fork process 3");
        return EXIT_FAILURE;
    }
    if (pid3 == 0) {
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
        for (int i = 0; i < NUM_VALUES; i++) {
            double x = read_buffer(shm_addr->buffer2, &shm_addr->head2,
                                   empty2, full2, mutex2);
            square_root(&x);
            random_delay();
            // Truncate display to match the whiteboard examples.
            // Calculations in the pipeline keep full double precision.
            double shown = floor(x * 100.0) / 100.0;
            printf("x = %d, Final = %.2f\n", i, shown);
            fflush(stdout);
        }
        _exit(EXIT_SUCCESS);
    }

    // Parent: Process 1 squares inputs and writes Buffer 1.
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (int i = 0; i < NUM_VALUES; i++) {
        double x = i;
        square(&x);
        random_delay();
        write_buffer(shm_addr->buffer1, &shm_addr->tail1, x,
                     empty1, full1, mutex1);
    }

    // Parent waits until both children finish using the buffers.
    waitpid(pid2, NULL, 0);
    waitpid(pid3, NULL, 0);

    // Only the parent removes shared resources.
    munmap(shm_addr, sizeof(SharedMemory));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    sem_close(empty1);
    sem_close(full1);
    sem_close(mutex1);
    sem_close(empty2);
    sem_close(full2);
    sem_close(mutex2);

    sem_unlink(SEM_EMPTY1);
    sem_unlink(SEM_FULL1);
    sem_unlink(SEM_MUTEX1);
    sem_unlink(SEM_EMPTY2);
    sem_unlink(SEM_FULL2);
    sem_unlink(SEM_MUTEX2);

    return EXIT_SUCCESS;
}
