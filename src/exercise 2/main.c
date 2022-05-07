#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include "fs/operations.h"

#define MAX_COMMANDS 10
#define MAX_INPUT_SIZE 100

enum { NS_PER_SECOND = 1000000000 };
int numberThreads = 0;
struct timespec start, finish, delta;

// commands' buffer
char inputCommands[MAX_COMMANDS][MAX_INPUT_SIZE];
int numberCommands = 0, insertPtr = 0, removePtr = 0;
int headQueue = 0;

// conditional variables
pthread_cond_t canInsert, canRemove;

// other
FILE *entrada;
pthread_mutex_t mutex;

int reachedEOF = 0;

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec  = t2.tv_sec - t1.tv_sec;
    if (td->tv_sec > 0 && td->tv_nsec < 0)
    {
        td->tv_nsec += NS_PER_SECOND;
        td->tv_sec--;
    }
    else if (td->tv_sec < 0 && td->tv_nsec > 0)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

int insertCommand(char* data) {
    
    pthread_mutex_lock(&mutex);
    while ( numberCommands == MAX_COMMANDS )
        pthread_cond_wait(&canInsert, &mutex);
    strcpy(inputCommands[insertPtr], data);
    insertPtr++;
    if ( insertPtr == MAX_COMMANDS )
        insertPtr = 0;
    numberCommands++;
    pthread_cond_signal(&canRemove);
    pthread_mutex_unlock(&mutex);
    return 1;
}

char* removeCommand() {

    char* command = (char*) malloc(sizeof(char)*MAX_INPUT_SIZE);

    pthread_mutex_lock(&mutex);
    while ( numberCommands == 0 && !reachedEOF )
        pthread_cond_wait(&canRemove, &mutex);
    if ( reachedEOF && numberCommands == 0 ) {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }
    strcpy(command, inputCommands[removePtr]);
    removePtr++;
    if ( removePtr == MAX_COMMANDS )
        removePtr = 0;
    numberCommands--;
    pthread_cond_signal(&canInsert);
    pthread_mutex_unlock(&mutex);
    return command;
}

void errorParse(){
    fprintf(stderr, "Error: command invalid\n");
    exit(EXIT_FAILURE);
}

void processInput(){
    char line[MAX_INPUT_SIZE];

    /* break loop with ^Z or ^D */
    clock_gettime(CLOCK_REALTIME, &start);
    while (fgets(line, sizeof(line)/sizeof(char), entrada)) {

        char token;
        char name[MAX_INPUT_SIZE];
        char arg2[MAX_INPUT_SIZE];

        /*usar f scanf aqui na linha de baixo*/
        int numTokens = sscanf(line, "%c %s %s", &token, name, arg2);

        /* perform minimal validation */
        if (numTokens < 1) {
            continue;
        }
        switch (token) {
            case 'c':
                if(numTokens != 3)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case 'l':
                if(numTokens != 2)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case 'd':
                if(numTokens != 2)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case 'm':
                if(numTokens != 3)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;

            case '#':
                break;
            
            default: { /* error */
                errorParse();
            }
        }
    }
    reachedEOF = 1;
    pthread_cond_broadcast(&canRemove);
}

void* applyCommands(){
    while ( numberCommands > 0 || !reachedEOF){
        const char* command = removeCommand();
        if (command == NULL){
            continue;
        }

        // locks por causa do requesito 2
        pthread_mutex_lock(&mutex);
        char token;
        char name[MAX_INPUT_SIZE];
        char arg2[MAX_INPUT_SIZE];
        int numTokens = sscanf(command, "%c %s %s", &token, name, arg2);
        pthread_mutex_unlock(&mutex);
        if (numTokens < 2) {
            fprintf(stderr, "Error: invalid command in Queue\n");
            exit(EXIT_FAILURE);
        }

        int searchResult;
        switch (token) {
            case 'c':
                switch (arg2[0]) {
                    case 'f':
                        printf("Create file: %s\n", name);
                        create(name, T_FILE);
                        break;
                    case 'd':
                        printf("Create directory: %s\n", name);
                        create(name, T_DIRECTORY);
                        break;
                    default:
                        fprintf(stderr, "Error: invalid node type\n");
                        exit(EXIT_FAILURE);
                }
                break;
            case 'l': 
                searchResult = lookup(name);
                if (searchResult >= 0)
                    printf("Search: %s found\n", name);
                else
                    printf("Search: %s not found\n", name);
                break;
            case 'd':
                printf("Delete: %s\n", name);
                delete(name);
                break;
            case 'm':
                printf("Move: %s to %s\n", name, arg2);
                move(name, arg2);
                break;
            default: { /* error */
                fprintf(stderr, "Error: command to apply\n");
                exit(EXIT_FAILURE);
            }
        }
        free( (char*) command);
    }
    pthread_exit(NULL);
}

void threadPool_init(pthread_t tid[]) {

    // error check
    if ( numberThreads <= 0 ) {
        fprintf(stderr, "Error: number of threads is zero.\n");
        exit(EXIT_FAILURE);
    }

    // starts all threads
    for ( int i = 0; i < numberThreads; i++ ) {
        pthread_create(&tid[i], NULL, applyCommands, NULL);
    }
}

void threadPool_destroy(pthread_t tid[]) {
    
    for ( int i = 0; i < numberThreads; i++ ) {
        if ( pthread_join(tid[i], NULL) != 0 ) {
            fprintf(stderr, "Error: a thread failed.\n");
            exit(EXIT_FAILURE);
        }
    }

}

int main(int argc, char* argv[]) {
    
    FILE *saida;

    // Opening of the entry file    
    entrada = fopen(argv[1],"r");
    if (entrada == NULL)
    {
      fprintf(stderr, "Error: entry file doesn't exist.\n");
        exit(EXIT_FAILURE);
    }

    // initializations
    if ( pthread_mutex_init(&mutex, NULL) != SUCCESS ) {
        fprintf(stderr, "Error: failed to initialize mutex.\n");
        exit(EXIT_FAILURE);
    }

    if ( pthread_cond_init(&canInsert, NULL) != SUCCESS ||
         pthread_cond_init(&canRemove, NULL) != SUCCESS )
    {
        fprintf(stderr, "Error: failed to initialize a conditional variable.\n");
        exit(EXIT_FAILURE);
    }

    numberThreads = atoi(argv[3]);
    pthread_t tid[numberThreads];


    /* init filesystem */
    init_fs();

    // applies commands
    threadPool_init(tid);
    
    /* fills commands vector (inputCommands) */
    processInput();

    threadPool_destroy(tid);

    saida = fopen(argv[2],"w");
    print_tecnicofs_tree(saida);
    
    /* release allocated memory */
    destroy_fs();
    
    if (pthread_mutex_destroy(&mutex) != SUCCESS)
    {
        fprintf(stderr, "Error: failed to destroy mutex lock");
        exit(EXIT_FAILURE);
    }
    
    if ( pthread_cond_destroy(&canInsert) != SUCCESS ||
         pthread_cond_destroy(&canRemove) != SUCCESS )
    {
        fprintf(stderr, "Error: failed to destroy a conditional variable");
        exit(EXIT_FAILURE);
    }
    
    //Closing both entry and output files
    fclose(saida);
    fclose(entrada);

    //end of the runtime of TecnicoFS
    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    double time_spent = (int)delta.tv_sec + ((double) delta.tv_nsec)*pow(10,-9);
    printf("TecnicoFS completed in %.4f seconds.\n",time_spent);
    exit(EXIT_SUCCESS);
}
