/* File:  
 *    pth_pool.c
 *
 * Purpose:
 *    Implementação de um pool de threads
 *
 *
 * Compile:  gcc -g -Wall -o pth_pool pth_pool.c -lpthread -lrt
 * Alternatively: make all
 * Usage:    ./pth_pool
 * Alternatively: make run
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <unistd.h>
#include <semaphore.h>
#include <time.h>

#define THREAD_NUM 6    // Tamanho do pool de threads
#define BUFFER_SIZE 16 // Númermo máximo de tarefas enfileiradas

typedef struct Clock {
    int p[3];
} Clock;

Clock globalClock = {{0,0,0}};
Clock taskQueue[BUFFER_SIZE];
int taskCount = 0;

pthread_mutex_t mutex;
pthread_mutex_t clock_mutex;

pthread_cond_t condFull;
pthread_cond_t condEmpty;

void executeTask(Clock* task, int id){
   pthread_mutex_lock(&clock_mutex);
   
   for (int i=0; i<3; i++) {
       if (globalClock.p[i] < task->p[i])
           globalClock.p[i] = task->p[i];
   }
   printf("(Consumidor %d) (%d, %d, %d)\n", id, globalClock.p[0], globalClock.p[1], globalClock.p[2]);

   pthread_mutex_unlock(&clock_mutex);
}

Clock getTask(){
   pthread_mutex_lock(&mutex);
   
   while (taskCount == 0){
      printf("BUFFER VAZIO\n");
      pthread_cond_wait(&condEmpty, &mutex);
   }
   Clock task = taskQueue[0];
   int i;
   for (i = 0; i < taskCount - 1; i++){
      taskQueue[i] = taskQueue[i+1];
   }
   taskCount--;
   
   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&condFull);
   return task;
}

void submitTask(Clock task){
   pthread_mutex_lock(&mutex);

   while (taskCount == BUFFER_SIZE){
      printf("BUFFER CHEIO\n");
      pthread_cond_wait(&condFull, &mutex);
   }

   taskQueue[taskCount] = task;
   taskCount++;

   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&condEmpty);
}

void *startThread(void* args);  

/*--------------------------------------------------------------------*/
int main(int argc, char* argv[]) {
   pthread_mutex_init(&mutex, NULL);
   pthread_mutex_init(&clock_mutex, NULL);
   pthread_cond_init(&condEmpty, NULL);
   pthread_cond_init(&condFull, NULL);

   pthread_t thread[THREAD_NUM]; 
   long i;
   for (i = 0; i < THREAD_NUM; i++){  
      if (pthread_create(&thread[i], NULL, &startThread, (void*) i) != 0) {
         perror("Failed to create the thread");
      }  
   }
   
   srand(time(NULL));
   
   for (i = 0; i < THREAD_NUM; i++){  
      if (pthread_join(thread[i], NULL) != 0) {
         perror("Failed to join the thread");
      }  
   }
   
   pthread_mutex_destroy(&mutex);
   pthread_mutex_destroy(&clock_mutex);
   pthread_cond_destroy(&condEmpty);
   pthread_cond_destroy(&condFull);
   return 0;
}  /* main */

/*-------------------------------------------------------------------*/
void *startThread(void* args) {
   long id = (long) args; 
   Clock localClock = {{0,0,0}};
   while (1){ 
      if (id < 3) {
         // ids 0, 1, 2: threads produtoras
         localClock.p[id]++;
         submitTask(localClock);
         // esperar ate 2 segundos para produzir mais do que o consumo
         sleep(rand()%4);
      }
      else {
         // ids 3, 4, 5: threads consumidoras
         Clock task = getTask();
         executeTask(&task, id-3);
         // esperar ate 4 segundos. para mudar o caso para consumir mais que a producao, so inverter as esperas
         sleep(rand()%2);
      }
      //sleep(rand()%5);
   }
   return NULL;
} 

