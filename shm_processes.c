/*
 * Lab 3 Part 2: System V shared memory with Tanenbaum strict alternation.
 * The shared block contains exactly two integers: BankAccount and Turn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define ITERATIONS 25

typedef enum {
    DAD_TURN = 0,
    STUDENT_TURN = 1
} TurnValue;

typedef struct {
    int BankAccount;
    int Turn;
} SharedState;

/* Yield while preserving the assignment's required busy-wait alternation. */
static void wait_for_turn(volatile SharedState *state, TurnValue expected) {
    while (state->Turn != (int)expected) {
        sched_yield();
    }
}

static void dad_process(volatile SharedState *state) {
    int iteration;
    srand((unsigned int)(time(NULL) ^ getpid()));

    for (iteration = 0; iteration < ITERATIONS; ++iteration) {
        int account;
        int balance;

        sleep((unsigned int)(rand() % 6));
        account = state->BankAccount;
        wait_for_turn(state, DAD_TURN);

        if (account <= 100) {
            balance = rand() % 101;
            if (balance % 2 == 0) {
                account += balance;
                printf("Dear old Dad: Deposits $%d / Balance = $%d\n",
                       balance, account);
            } else {
                printf("Dear old Dad: Doesn't have any money to give\n");
            }
        } else {
            printf("Dear old Dad: Thinks Student has enough Cash ($%d)\n",
                   account);
        }

        /* Publish the new balance before handing control to the student. */
        state->BankAccount = account;
        state->Turn = STUDENT_TURN;
        fflush(stdout);
    }
}

static void student_process(volatile SharedState *state) {
    int iteration;
    srand((unsigned int)(time(NULL) ^ getpid()));

    for (iteration = 0; iteration < ITERATIONS; ++iteration) {
        int account;
        int balance;

        sleep((unsigned int)(rand() % 6));
        account = state->BankAccount;
        wait_for_turn(state, STUDENT_TURN);

        balance = rand() % 51;
        printf("Poor Student needs $%d\n", balance);
        if (balance <= account) {
            account -= balance;
            printf("Poor Student: Withdraws $%d / Balance = $%d\n",
                   balance, account);
        } else {
            printf("Poor Student: Not Enough Cash ($%d)\n", account);
        }

        /* Publish the new balance before handing control back to Dad. */
        state->BankAccount = account;
        state->Turn = DAD_TURN;
        fflush(stdout);
    }
}

/* Detach this process and mark the segment for removal by the kernel. */
static int release_shared_memory(volatile SharedState *state, int shmid) {
    int failed = 0;
    if (shmdt((const void *)state) == -1) {
        perror("shmdt");
        failed = 1;
    }
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl");
        failed = 1;
    }
    return failed ? -1 : 0;
}

int main(void) {
    int shmid;
    int status = 0;
    int wait_failed = 0;
    volatile SharedState *state;
    pid_t child;

    shmid = shmget(IPC_PRIVATE, sizeof(SharedState), IPC_CREAT | 0600);
    if (shmid == -1) {
        perror("shmget");
        return EXIT_FAILURE;
    }

    state = (volatile SharedState *)shmat(shmid, NULL, 0);
    if (state == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }

    state->BankAccount = 0;
    state->Turn = DAD_TURN;

    child = fork();
    if (child < 0) {
        perror("fork");
        release_shared_memory(state, shmid);
        return EXIT_FAILURE;
    }

    if (child == 0) {
        student_process(state);
        if (shmdt((const void *)state) == -1) {
            perror("shmdt");
            _exit(EXIT_FAILURE);
        }
        _exit(EXIT_SUCCESS);
    }

    dad_process(state);

    /* Reap the child before the parent detaches and removes the segment. */
    if (waitpid(child, &status, 0) == -1) {
        perror("waitpid");
        wait_failed = 1;
    }

    if (release_shared_memory(state, shmid) == -1) {
        return EXIT_FAILURE;
    }
    if (wait_failed || !WIFEXITED(status) ||
        WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Child process failed.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
