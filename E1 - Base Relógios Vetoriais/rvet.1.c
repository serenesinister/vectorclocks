/**
 * Implementação de relógios vetoriais usando MPI baseada no exemplo da URL:
 * https://people.cs.rutgers.edu/~pxk/417/notes/images/clocks-vector.png
 * 
 * Compilação: mpicc -o rvet.1 rvet.1.c
 * Execução: mpiexec -n 3 ./rvet.1
 */

#include <stdio.h>
#include <string.h>
#include <mpi.h>

typedef struct Clock {
    int p[3];
} Clock;

void printClock(int pid, Clock *clock, const char label, int updateType, char secondLabel) {
    switch (updateType) {
        case 0: printf("P%d|%c (%d, %d, %d) evento interno\n", pid, label, clock->p[0], clock->p[1], clock->p[2]); break;
        case 1: printf("P%d|%c (%d, %d, %d) envio para %c\n", pid, label, clock->p[0], clock->p[1], clock->p[2], secondLabel); break;
        case 2: printf("P%d|%c (%d, %d, %d) recebido de %c\n", pid, label, clock->p[0], clock->p[1], clock->p[2], secondLabel); break;
    }
}

void evento(int pid, Clock *clock, char label) {
    clock->p[pid]++;
    printClock(pid, clock, label, 0, 0);
}

void envio(int pid, Clock *clock, int dest, char label, char destLabel) {
    clock->p[pid]++;
    MPI_Send(clock->p, 3, MPI_INT, dest, 0, MPI_COMM_WORLD);
    printClock(pid, clock, label, 1, destLabel);
}

void recebe(int pid, Clock *clock, int source, char label, char sourceLabel) {
    int msg[3];
    MPI_Recv(msg, 3, MPI_INT, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i = 0; i < 3; i++) {
        if (msg[i] > clock->p[i]) {
            clock->p[i] = msg[i];
        }
    }
    clock->p[pid]++;
    printClock(pid, clock, label, 2, sourceLabel);
}


// Processo 0
void process0() {
    Clock clock = {{0, 0, 0}};
    evento(0, &clock, 'a');  // a

    envio(0, &clock, 1, 'b', 'l');  // b -> i

    recebe(0, &clock, 1, 'c', 'h');  // c (recebe de h)

    envio(0, &clock, 2, 'd', 'm');  // d -> m

    recebe(0, &clock, 2, 'e', 'l');  // e (recebe de l)

    envio(0, &clock, 1, 'f', 'j');  // f -> j

    evento(0, &clock, 'g');  // g
}

// Processo 1
void process1() {
    Clock clock = {{0, 0, 0}};

    envio(1, &clock, 0, 'h', 'c');  // h -> c
    
    recebe(1, &clock, 0, 'i', 'b');  // i (recebe de b)

    recebe(1, &clock, 0, 'j', 'f');  // j (recebe de f)
}

// Processo 2
void process2() {
    Clock clock = {{0, 0, 0}};
    evento(2, &clock, 'k');  // k

    envio(2, &clock, 0, 'l', 'e');  // l -> e

    recebe(2, &clock, 0, 'm', 'd');  // m (recebe de d)
}

int main(void) {
    int my_rank;

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    if (my_rank == 0) {
        process0();
    } else if (my_rank == 1) {
        process1();
    } else if (my_rank == 2) {
        process2();
    }

    MPI_Finalize();
    return 0;
}

/*
P0 (1,0,0)

a (1,0,0) interno
b (2,0,0) envia b->i
c (3,1,0) recebe c<-h
d (4,1,0) envia d->m
e (5,1,2) recebe e<-l
f (6,1,2) envia f->j
g (7,1,2) interno

P1 (0,1,0)

h (0,1,0) envia h->i
i (2,2,0) recebe i<-b
j (6,3,2) recebe j<-f

P2 (0,0,1)

k (0,0,1) interno
l (0,0,2) envia l->e
m (4,1,3) recebe m<-d
*/