/**
 * Etapa 4
 * Implementação dos Snaphots de Chandy-Lamport sobre os relógios vetoriais da Etapa 3
 * 
 * Compilação: mpicc -o rvet_snapshot rvet_snapshot.c -lpthread
 * Execução: mpiexec -n 3 ./rvet_snapshot
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
#define MAX_QUEUE 32

/* ----------------------------- Relógio Vetorial ---------------------------- */

typedef struct Clock {
    int p[NUM_PROC];
} Clock;

static inline void clock_max(Clock *dst, const Clock *src) {
    for (int i = 0; i < NUM_PROC; i++)
        if (src->p[i] > dst->p[i]) dst->p[i] = src->p[i];
}

/* --------------------------------- Eventos --------------------------------- */

typedef enum { EVENTO, ENVIO, RECEBIMENTO } TipoEvento;

typedef struct Evento {
    TipoEvento tipo;
    char label;
    int destino_ou_origem;
    char outroLabel;
} Evento;

/* ------------------------------- Mensagens MPI ----------------------------- */

typedef enum { MSG_NORMAL = 1, MSG_MARKER = 2 } MsgType;

typedef struct Msg {
    int type;
    int from;
    int to;
    char label;
    Clock clock;
} Msg;

/* ------------------------------ Filas Thread-Safe -------------------------- */

typedef struct {
    Evento buf[MAX_QUEUE];
    int ini, fim, size;
    pthread_mutex_t m;
    pthread_cond_t c;
} FilaEvento;

static void filaEvento_init(FilaEvento *q){ q->ini=q->fim=q->size=0; pthread_mutex_init(&q->m,NULL); pthread_cond_init(&q->c,NULL);} 
static void filaEvento_push(FilaEvento *q, Evento ev){
    pthread_mutex_lock(&q->m);
    while(q->size==MAX_QUEUE) pthread_cond_wait(&q->c,&q->m);
    q->buf[q->fim]=ev; q->fim=(q->fim+1)%MAX_QUEUE; q->size++; pthread_cond_broadcast(&q->c); pthread_mutex_unlock(&q->m);
}
static Evento filaEvento_pop(FilaEvento *q, volatile int *running){
    pthread_mutex_lock(&q->m);
    while(q->size==0 && *running) pthread_cond_wait(&q->c,&q->m);
    Evento ev={EVENTO,'?',-1,'?'}; if(q->size){ ev=q->buf[q->ini]; q->ini=(q->ini+1)%MAX_QUEUE; q->size--; }
    pthread_cond_broadcast(&q->c); pthread_mutex_unlock(&q->m); return ev; }

//fila para mensagens recebidas (ordem de chegada)

typedef struct {
    Msg buf[MAX_QUEUE];
    int ini, fim, size;
    pthread_mutex_t m;
    pthread_cond_t c;
} FilaMsg;

static void filaMsg_init(FilaMsg *q){ q->ini=q->fim=q->size=0; pthread_mutex_init(&q->m,NULL); pthread_cond_init(&q->c,NULL);} 
static void filaMsg_push(FilaMsg *q, Msg m){
    pthread_mutex_lock(&q->m);
    while(q->size==MAX_QUEUE) pthread_cond_wait(&q->c,&q->m);
    q->buf[q->fim]=m; q->fim=(q->fim+1)%MAX_QUEUE; q->size++; pthread_cond_broadcast(&q->c); pthread_mutex_unlock(&q->m);
}
static Msg filaMsg_pop(FilaMsg *q, volatile int *running){
    pthread_mutex_lock(&q->m);
    while(q->size==0 && *running) pthread_cond_wait(&q->c,&q->m);
    Msg m={0}; if(q->size){ m=q->buf[q->ini]; q->ini=(q->ini+1)%MAX_QUEUE; q->size--; }
    pthread_cond_broadcast(&q->c); pthread_mutex_unlock(&q->m); return m; }

/* --------------------------- Snapshot Chandy-Lamport ----------------------- */

typedef struct Snapshot {
    int active; //snapshot em andamento?
    Clock local; //estado local gravado
    int marker_recv[NUM_PROC]; //para cada canal de entrada, recebi marker?
    char channel_labels[NUM_PROC][MAX_QUEUE];
    int channel_counts[NUM_PROC];
    pthread_mutex_t m;
} Snapshot;

static void snapshot_init(Snapshot *s){
    s->active=0; memset(&s->local,0,sizeof(Clock)); memset(s->marker_recv,0,sizeof(s->marker_recv));
    memset(s->channel_labels,0,sizeof(s->channel_labels)); memset(s->channel_counts,0,sizeof(s->channel_counts));
    pthread_mutex_init(&s->m,NULL);
}

/* --------------------------------- Contexto -------------------------------- */

typedef struct Contexto {
    int pid;
    Clock clock;
    FilaMsg inbox; //mensagens recebidas (para RECEBIMENTO)
    FilaEvento outbox; //pedidos de ENVIO vindos da timeline
    volatile int running;
    Snapshot snap;
} Contexto;

/* --------------------------------- Registro -------------------------------- */

static void printClock(int pid, Clock *clock, char label, TipoEvento tipo, char secondLabel) {
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

/* ---------------------------- MPI send recv -------------------------------- */

static void send_msg(const Msg *m){
    MPI_Send((void*)m, sizeof(Msg), MPI_BYTE, m->to, 0, MPI_COMM_WORLD);
}

static int recv_msg(int *src_opt, Msg *out, MPI_Status *status){
    int flag=0; MPI_Iprobe(src_opt?*src_opt:MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &flag, status);
    if(!flag) return 0;
    MPI_Recv(out, sizeof(Msg), MPI_BYTE, status->MPI_SOURCE, 0, MPI_COMM_WORLD, status);
    return 1;
}

/* ------------------------------ Threads ------------------------------------ */

static void start_snapshot(Contexto *ctx){
    pthread_mutex_lock(&ctx->snap.m);

    if(ctx->snap.active){ 
        pthread_mutex_unlock(&ctx->snap.m); 
        return; 
    }

    //aguarda que a fila de recebimento esteja vazia antes de salvar estado local
    while(ctx->inbox.size > 0){
        pthread_mutex_unlock(&ctx->snap.m);
        usleep(1000);
        pthread_mutex_lock(&ctx->snap.m);
    }

    //inicia snapshot
    ctx->snap.active = 1;
    ctx->snap.local = ctx->clock; 
    for(int i=0;i<NUM_PROC;i++){
        ctx->snap.marker_recv[i] = (i == ctx->pid);
        ctx->snap.channel_counts[i] = 0;
        memset(ctx->snap.channel_labels[i], 0, MAX_QUEUE); // limpa canais
    }

    //envia markers para todos os outros processos
    for(int p=0;p<NUM_PROC;p++){
        if(p != ctx->pid){
            Msg mk = {.type = MSG_MARKER, .from = ctx->pid, .to = p, .label='M'};
            send_msg(&mk);
        }
    }

    pthread_mutex_unlock(&ctx->snap.m);
}

static void *threadEntrada(void *arg){
    Contexto *ctx = (Contexto*)arg;

    while(ctx->running){
        MPI_Status st; 
        Msg m;

        if(!recv_msg(NULL, &m, &st)){ 
            usleep(1000); 
            continue; 
        }

        pthread_mutex_lock(&ctx->snap.m);

        if(m.type == MSG_MARKER){
            int from = m.from;

            if(!ctx->snap.active){
                //espera que todas as mensagens antigas sejam processadas antes de salvar estado
                while(ctx->inbox.size > 0){
                    pthread_mutex_unlock(&ctx->snap.m);
                    usleep(1000);
                    pthread_mutex_lock(&ctx->snap.m);
                }

                //grava estado local ao receber o primeiro marker
                ctx->snap.active = 1; 
                ctx->snap.local = ctx->clock; 

                for(int i=0;i<NUM_PROC;i++){
                    ctx->snap.marker_recv[i] = (i == from);
                    ctx->snap.channel_counts[i] = 0;
                    memset(ctx->snap.channel_labels[i], 0, MAX_QUEUE);
                }

                //envia markers para todos os outros processos
                for(int p=0;p<NUM_PROC;p++){
                    if(p != ctx->pid){
                        Msg mk = {.type=MSG_MARKER, .from=ctx->pid, .to=p, .label='M'};
                        send_msg(&mk);
                    }
                }
            } else {
                ctx->snap.marker_recv[from] = 1;
            }

            //verifica se todos os markers chegaram
            int done = 1;
            for(int p=0;p<NUM_PROC;p++){
                if(p == ctx->pid) continue;
                if(!ctx->snap.marker_recv[p]){ done=0; break; }
            }

            if(done){
                printf("\n=== SNAPSHOT em P%d ===\n", ctx->pid);
                printf("Local: (%d,%d,%d)\n", ctx->snap.local.p[0], ctx->snap.local.p[1], ctx->snap.local.p[2]);
                for(int p=0;p<NUM_PROC;p++){
                    if(p == ctx->pid) continue;
                    printf("Canal %d->%d: ", p, ctx->pid);
                    int n = ctx->snap.channel_counts[p];
                    if(n == 0) printf("<vazio>\n");
                    else {
                        for(int i=0;i<n;i++) putchar(ctx->snap.channel_labels[p][i]);
                        putchar('\n');
                    }
                }
                printf("======================\n\n");

                //reseta snapshot para permitir outro disparo
                ctx->snap.active = 0; 
                memset(ctx->snap.marker_recv, 0, sizeof(ctx->snap.marker_recv));
                memset(ctx->snap.channel_counts, 0, sizeof(ctx->snap.channel_counts));
                for(int p=0;p<NUM_PROC;p++) memset(ctx->snap.channel_labels[p], 0, MAX_QUEUE);
            }

            pthread_mutex_unlock(&ctx->snap.m);
            continue; //marker não vai para aplicação
        } else {
            //mensagem normal: se snapshot ativo e canal ainda não recebeu marker, grava como em trânsito
            if(ctx->snap.active && !ctx->snap.marker_recv[m.from]){
                int k = ctx->snap.channel_counts[m.from];
                if(k < MAX_QUEUE){ 
                    ctx->snap.channel_labels[m.from][k] = m.label; 
                    ctx->snap.channel_counts[m.from]++; 
                }
            }
            pthread_mutex_unlock(&ctx->snap.m);

            //encaminha mensagem para fila de entrega à aplicação
            filaMsg_push(&ctx->inbox, m);
        }
    }

    return NULL;
}

static void *threadSaida(void *arg){
    Contexto *ctx=(Contexto*)arg;
    while(ctx->running){
        Evento ev = filaEvento_pop(&ctx->outbox, &ctx->running);
        if(!ctx->running) break;
        if(ev.tipo!=ENVIO) continue;
        ctx->clock.p[ctx->pid]++;
        Msg m={.type=MSG_NORMAL,.from=ctx->pid,.to=ev.destino_ou_origem,.label=ev.label};
        m.clock = ctx->clock;
        send_msg(&m);
        printClock(ctx->pid, &ctx->clock, ev.label, ENVIO, ev.outroLabel);
    }
    return NULL;
}

static void *threadRelogio(void *arg){
    Contexto *ctx=(Contexto*)arg; int pid=ctx->pid;

    Evento eventos_p0[] = {
        {EVENTO, 'a', -1, 0},
        {ENVIO,  'b', 1, 'i'},
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

    Evento *lista; int count;
    if(pid==0){ lista=eventos_p0; count=sizeof(eventos_p0)/sizeof(Evento);} 
    else if(pid==1){ lista=eventos_p1; count=sizeof(eventos_p1)/sizeof(Evento);} 
    else { lista=eventos_p2; count=sizeof(eventos_p2)/sizeof(Evento);} 

    for(int i=0;i<count;i++){
        Evento ev = lista[i];
        if(ev.tipo==EVENTO){
            ctx->clock.p[pid]++; printClock(pid,&ctx->clock,ev.label,EVENTO,0);
            // dispara o snapshot após o primeiro envio de P0
            if(pid==0 && ev.label=='a'){
                start_snapshot(ctx);
            }
        } else if(ev.tipo==ENVIO){
            filaEvento_push(&ctx->outbox, ev);
        } else if(ev.tipo==RECEBIMENTO){
            //espera alguma mensagem e entrega
            Msg m = filaMsg_pop(&ctx->inbox, &ctx->running);
            if(!ctx->running) break;
            //integra relógio
            clock_max(&ctx->clock, &m.clock);
            ctx->clock.p[pid]++;
            printClock(pid,&ctx->clock,ev.label,RECEBIMENTO,ev.outroLabel);
        }
        usleep(100000);
    }
    pthread_exit(NULL);
}

int main(){
    int provided=0; MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    if(provided < MPI_THREAD_MULTIPLE){
        fprintf(stderr,"MPI não suporta MPI_THREAD_MULTIPLE neste ambiente.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    int pid; MPI_Comm_rank(MPI_COMM_WORLD,&pid);

    Contexto ctx; ctx.pid=pid; ctx.running=1; memset(&ctx.clock,0,sizeof(Clock));
    filaMsg_init(&ctx.inbox); filaEvento_init(&ctx.outbox); snapshot_init(&ctx.snap);

    pthread_t tIn, tOut, tRel;
    pthread_create(&tIn,NULL,threadEntrada,&ctx);
    pthread_create(&tOut,NULL,threadSaida,&ctx);
    pthread_create(&tRel,NULL,threadRelogio,&ctx);

    pthread_join(tRel,NULL);
    ctx.running=0; 
    pthread_cond_broadcast(&ctx.inbox.c); pthread_cond_broadcast(&ctx.outbox.c);
    pthread_join(tIn,NULL); pthread_join(tOut,NULL);

    MPI_Finalize();
    return 0;
}

/* REFERÊNCIA esperada para os relógios
P0:
 a (1,0,0) interno
 b (2,0,0) envio b->i
 c (3,1,0) recebe c<-h
 d (4,1,0) envio d->m
 e (5,1,2) recebe e<-l
 f (6,1,2) envio f->j
 g (7,1,2) interno

P1:
 h (0,1,0) envio h->c
 i (2,2,0) recebe i<-b
 j (6,3,2) recebe j<-f

P2:
 k (0,0,1) interno
 l (0,0,2) envio l->e
 m (4,1,3) recebe m<-d
*/
