/**
 * Implementação de relógios vetoriais usando MPI baseada no exemplo da URL:
 * https://people.cs.rutgers.edu/~pxk/417/notes/images/clocks-vector.png
 * 
 * Compilação: mpicc -o rvet_pth rvet_pth.c -lpthread
 * Execução: mpiexec -n 3 ./rvet_pth
 * 
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mpi.h>
#include <unistd.h>

#define NUM_PROC 3
#define MAX_QUEUE 10

typedef struct Clock {
    int p[NUM_PROC];
} Clock;

typedef enum { EVENTO, ENVIO, RECEBIMENTO } TipoEvento;

typedef struct Evento {
    TipoEvento tipo;
    char label;
    int destino_ou_origem;
    char outroLabel;
} Evento;

typedef struct Fila {
    Evento eventos[MAX_QUEUE];
    int inicio, fim, tamanho;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Fila;

typedef struct Contexto {
    int pid;
    Clock clock;
    Fila filaEntrada;
    Fila filaSaida;
    volatile int running;
} Contexto;

void initFila(Fila *fila) {
    fila->inicio = fila->fim = fila->tamanho = 0;
    pthread_mutex_init(&fila->mutex, NULL);
    pthread_cond_init(&fila->cond, NULL);
}

void pushFila(Fila *fila, Evento ev) {
    pthread_mutex_lock(&fila->mutex);
    while (fila->tamanho == MAX_QUEUE)
        pthread_cond_wait(&fila->cond, &fila->mutex);
    fila->eventos[fila->fim] = ev;
    fila->fim = (fila->fim + 1) % MAX_QUEUE;
    fila->tamanho++;
    pthread_cond_broadcast(&fila->cond);
    pthread_mutex_unlock(&fila->mutex);
}

Evento popFila(Fila *fila, volatile int *running) {
    pthread_mutex_lock(&fila->mutex);
    while (fila->tamanho == 0 && *running)
        pthread_cond_wait(&fila->cond, &fila->mutex);

    Evento ev;
    if (fila->tamanho > 0) {
        ev = fila->eventos[fila->inicio];
        fila->inicio = (fila->inicio + 1) % MAX_QUEUE;
        fila->tamanho--;
    } else {
        // dummy event in case of shutdown
        ev.tipo = EVENTO;
        ev.label = '?';
        ev.destino_ou_origem = -1;
        ev.outroLabel = '?';
    }
    pthread_cond_broadcast(&fila->cond);
    pthread_mutex_unlock(&fila->mutex);
    return ev;
}

void printClock(int pid, Clock *clock, char label, TipoEvento tipo, char secondLabel) {
    switch (tipo) {
        case EVENTO:
            printf("P%d|%c (%d, %d, %d) evento interno\n", pid, label, clock->p[0], clock->p[1], clock->p[2]);
            break;
        case ENVIO:
            printf("P%d|%c (%d, %d, %d) envio para %c\n", pid, label, clock->p[0], clock->p[1], clock->p[2], secondLabel);
            break;
        case RECEBIMENTO:
            printf("P%d|%c (%d, %d, %d) recebido de %c\n", pid, label, clock->p[0], clock->p[1], clock->p[2], secondLabel);
            break;
    }
    fflush(stdout);
}

void* threadEntrada(void* arg) {
    Contexto *ctx = (Contexto*) arg;
    while (ctx->running) {
        int flag = 0;
        MPI_Status status;

        // Verifica se há mensagem disponível
        MPI_Iprobe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &flag, &status);
        if (flag) {
            int msg[NUM_PROC];
            MPI_Recv(msg, NUM_PROC, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

            Evento ev;
            ev.tipo = RECEBIMENTO;
            ev.destino_ou_origem = status.MPI_SOURCE;
            ev.label = '?';
            ev.outroLabel = '?';
            memcpy(&ev.label, msg, sizeof(int) * NUM_PROC);

            pushFila(&ctx->filaEntrada, ev);
        } else {
            // Não há mensagem, dá uma pausa curta para evitar busy waiting
            usleep(1000);
        }
    }
    return NULL;
}

void* threadSaida(void* arg) {
    Contexto *ctx = (Contexto*) arg;
    while (ctx->running) {
        Evento ev = popFila(&ctx->filaSaida, &ctx->running);
        if (!ctx->running) break;
        ctx->clock.p[ctx->pid]++;
        MPI_Send(ctx->clock.p, NUM_PROC, MPI_INT, ev.destino_ou_origem, 0, MPI_COMM_WORLD);
        printClock(ctx->pid, &ctx->clock, ev.label, ENVIO, ev.outroLabel);
    }
    return NULL;
}

void* threadRelogio(void* arg) {
    Contexto *ctx = (Contexto*) arg;
    int pid = ctx->pid;

    Evento eventos_p0[] = {
        {EVENTO, 'a', -1, 0},
        {ENVIO,  'b', 1, 'l'},
        {RECEBIMENTO, 'c', 1, 'h'},
        {ENVIO,  'd', 2, 'm'},
        {RECEBIMENTO, 'e', 2, 'l'},
        {ENVIO,  'f', 1, 'j'},
        {EVENTO, 'g', -1, 0}
    };

    Evento eventos_p1[] = {
        {ENVIO, 'h', 0, 'c'},
        {RECEBIMENTO, 'i', 0, 'b'},
        {RECEBIMENTO, 'j', 0, 'f'}
    };

    Evento eventos_p2[] = {
        {EVENTO, 'k', -1, 0},
        {ENVIO,  'l', 0, 'e'},
        {RECEBIMENTO, 'm', 0, 'd'}
    };

    Evento *lista;
    int count;
    if (pid == 0) {
        lista = eventos_p0; count = sizeof(eventos_p0)/sizeof(Evento);
    } else if (pid == 1) {
        lista = eventos_p1; count = sizeof(eventos_p1)/sizeof(Evento);
    } else {
        lista = eventos_p2; count = sizeof(eventos_p2)/sizeof(Evento);
    }

    for (int i = 0; i < count; i++) {
        Evento ev = lista[i];

        if (ev.tipo == EVENTO) {
            ctx->clock.p[pid]++;
            printClock(pid, &ctx->clock, ev.label, EVENTO, 0);
        } else if (ev.tipo == ENVIO) {
            pushFila(&ctx->filaSaida, ev);
        } else if (ev.tipo == RECEBIMENTO) {
            while (ctx->running) {
                Evento recv = popFila(&ctx->filaEntrada, &ctx->running);
                int *msg = (int*)&recv.label;
                for (int i = 0; i < NUM_PROC; i++) {
                    if (msg[i] > ctx->clock.p[i])
                        ctx->clock.p[i] = msg[i];
                }
                ctx->clock.p[pid]++;
                printClock(pid, &ctx->clock, ev.label, RECEBIMENTO, ev.outroLabel);
                break;
            }
        }
        usleep(100000); // delay para simular tempo
    }
    pthread_exit(NULL);
}

int main() {
    int pid;
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);

    Contexto ctx;
    ctx.pid = pid;
    ctx.running = 1;
    memset(&ctx.clock, 0, sizeof(Clock));
    initFila(&ctx.filaEntrada);
    initFila(&ctx.filaSaida);

    pthread_t tEntrada, tRelogio, tSaida;

    pthread_create(&tEntrada, NULL, threadEntrada, &ctx);
    pthread_create(&tSaida, NULL, threadSaida, &ctx);
    pthread_create(&tRelogio, NULL, threadRelogio, &ctx);

    pthread_join(tRelogio, NULL);

    ctx.running = 0;

    pthread_cond_broadcast(&ctx.filaEntrada.cond);
    pthread_cond_broadcast(&ctx.filaSaida.cond);

    pthread_join(tEntrada, NULL);
    pthread_join(tSaida, NULL);

    MPI_Finalize();
    return 0;
}


/* REFERÊNCIA
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
