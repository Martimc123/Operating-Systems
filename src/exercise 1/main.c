#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "fs/operations.h"

#define MAX_COMMANDS 150000
#define MAX_INPUT_SIZE 100

// constants for main argumet's indexes
#define INPUT_FILE      1
#define OUTPUT_FILE     2
#define NUM_THREADS     3
#define SYNC_STRAT      4

int numberThreads = 0;

char inputCommands[MAX_COMMANDS][MAX_INPUT_SIZE];
int numberCommands = 0;
int headQueue = 0;

char* syncStrategy;
pthread_mutex_t mutex;
pthread_rwlock_t rwlock;

/*
 * Locks a critical zone with the lock of choice, if nosync strategie wasn't chosen.
 *      syncStrategy = "mutex", rwSelect = (any char)
 *          locks with mutex
 *      rwSelect = 'm'
 *          forces a lock with mutex, independant of syncStrategy
 *      
 *     syncStrategy = "rwlock",
 *          rwSelect = 'w'
 *              locks with wrlock
 *          rwSelect = 'r'
 *              locks with rdlock
 */
void lockCritSect(char rwSelect) {
    
    if ( strcmp(syncStrategy, "nosync") == 0 )
        return;
    
    if ( strcmp(syncStrategy, "mutex") == 0 || rwSelect == 'm' ) {
        if ( pthread_mutex_lock(&mutex) != 0 ) {
            fprintf(stderr, "Error: failed to lock mutex.\n");
            exit(EXIT_FAILURE);
        }
    }
    
    if ( strcmp(syncStrategy, "rwlock") == 0 ) {
        if ( rwSelect == 'w' ) {
            if ( pthread_rwlock_wrlock(&rwlock) != 0 ) {
                fprintf(stderr, "Error: failed to lock wrlock.\n");
                exit(EXIT_FAILURE);
            }
        }
        if ( rwSelect == 'r' ) {
            if ( pthread_rwlock_rdlock(&rwlock) != 0 ) {
                fprintf(stderr, "Error: failed to lock rdlock.\n");
                exit(EXIT_FAILURE);
            }
        }
        
    }
}

/*
 * Unlocks the respective lock.
 */
void unlockCritSect() {
    
    //printf("firstunlock\n");

    if ( strcmp(syncStrategy, "nosync") == 0 )
        return;
    
    if ( strcmp(syncStrategy, "mutex") == 0 ) {
        if ( pthread_mutex_unlock(&mutex) != 0 ) {
            fprintf(stderr, "Error, failed to unlock mutex.\n");
            exit(EXIT_FAILURE);
        }
    }
    
    if ( strcmp(syncStrategy, "rwlock") == 0 ) {
        if ( pthread_rwlock_unlock(&rwlock) != 0 ) {
            fprintf(stderr, "Error, failed to unlock rwlock.\n");
            exit(EXIT_FAILURE);
        }
    }
}

int insertCommand(char* data) {
    
    pthread_mutex_lock(&mutex);
    if(numberCommands != MAX_COMMANDS) {
        strcpy(inputCommands[numberCommands++], data);
        pthread_mutex_unlock(&mutex);
        return 1;
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}

char* removeCommand() {
    
    pthread_mutex_lock(&mutex);
    if(numberCommands > 0){
        numberCommands--;
        pthread_mutex_unlock(&mutex);
        return inputCommands[headQueue++];
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

void errorParse(){
    fprintf(stderr, "Error: command invalid\n");
    exit(EXIT_FAILURE);
}

void processInput(FILE *ficheiro){
    char line[MAX_INPUT_SIZE];

    /* break loop with ^Z or ^D */
    while (fgets(line, sizeof(line)/sizeof(char), ficheiro)) {
        char token, type;
        char name[MAX_INPUT_SIZE];

        /*usar f scanf aqui na linha de baixo*/
        int numTokens = sscanf(line, "%c %s %c", &token, name, &type);

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
            
            case '#':
                break;
            
            default: { /* error */
                errorParse();
            }
        }
    }
}

void* applyCommands(){
    while (numberCommands > 0){
        const char* command = removeCommand();
        if (command == NULL){
            continue;
        }

        //lockCritSect('w');

        char token, type;
        char name[MAX_INPUT_SIZE];
        int numTokens = sscanf(command, "%c %s %c", &token, name, &type);
        if (numTokens < 2) {
            fprintf(stderr, "Error: invalid command in Queue\n");
            exit(EXIT_FAILURE);
        }

        int searchResult;
        switch (token) {
            case 'c':
                switch (type) {
                    case 'f':
                        lockCritSect('w');
                        //sleep(1);
                        printf("Create file: %s\n", name);
                        create(name, T_FILE);
                        unlockCritSect();
                        break;
                    case 'd':
                        lockCritSect('w');
                        //sleep(1);
                        printf("Create directory: %s\n", name);
                        create(name, T_DIRECTORY);
                        unlockCritSect();
                        break;
                    default:
                        fprintf(stderr, "Error: invalid node type\n");
                        exit(EXIT_FAILURE);
                }
                break;
            case 'l': 
                lockCritSect('r');
                searchResult = lookup(name);
                if (searchResult >= 0)
                    printf("Search: %s found\n", name);
                else
                    printf("Search: %s not found\n", name);
                unlockCritSect();
                break;
            case 'd':
                lockCritSect('r');
                //sleep(1);
                printf("Delete: %s\n", name);
                delete(name);
                unlockCritSect();
                break;
            default: { /* error */
                fprintf(stderr, "Error: command to apply\n");
                exit(EXIT_FAILURE);
            }
        }
        //unlockCritSect();
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
        if ( pthread_create(&tid[i], NULL, applyCommands, NULL) != 0) {
            fprintf(stderr, "Error: thread creation failed.\n");
            exit(EXIT_FAILURE);
        }
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
    
    FILE *inputFile;                // pointer to input file
    FILE *outputFile;               // pointer to output file
    clock_t clockBegin = clock();   // clock begins
    numberThreads = atoi(argv[NUM_THREADS]);  //
    pthread_t tid[numberThreads];   // array of threads // TO-DO: precisa de ser global?
    
    pthread_mutex_init(&mutex, NULL);
    pthread_rwlock_init(&rwlock, NULL);


    // threads and sync strategy setup
    syncStrategy = (char*) malloc( sizeof(char) * (strlen(argv[SYNC_STRAT]) + 1) );
    strcpy(syncStrategy, argv[SYNC_STRAT]);          // TO-DO: precisa de ter error check?
    if ( strcmp(syncStrategy, "nosync") == 0 && numberThreads > 1) {
        fprintf(stderr, "Error: nosync strategy must have only one thread.\n");
        exit(EXIT_FAILURE);
    }

    // initialize filesystem
    init_fs();

    // fills commands vector (inputCommands)
    inputFile = fopen(argv[INPUT_FILE],"r");
    if ( inputFile == NULL ) {
        fprintf(stderr, "Error: failed to open input file.\n");
        exit(EXIT_FAILURE);
    }
    processInput(inputFile);
    if ( fclose(inputFile) != 0 ) {
        fprintf(stderr, "Error: failed to close input file.\n");
        exit(EXIT_FAILURE);
    }

    // applies commands
    threadPool_init(tid);
    threadPool_destroy(tid);

    // fills output file
    outputFile = fopen(argv[OUTPUT_FILE],"w");
    if ( outputFile == NULL ) {
        fprintf(stderr, "Error: failed to open output file.\n");
        exit(EXIT_FAILURE);
    }
    print_tecnicofs_tree(outputFile);
    if ( fclose(outputFile) != 0 ) {
        fprintf(stderr, "Error: failed to close output file.\n");
        exit(EXIT_FAILURE);
    }
    
    // release allocated memory
    destroy_fs();
    free(syncStrategy);
    pthread_mutex_destroy(&mutex);
    pthread_rwlock_destroy(&rwlock);
    
    // ends clock
    clock_t end = clock();
    double time_spent = (double)(end - clockBegin) / CLOCKS_PER_SEC;
    printf("TecnicoFS completed in %.3f seconds.\n", time_spent);
    
    exit(EXIT_SUCCESS);
}
