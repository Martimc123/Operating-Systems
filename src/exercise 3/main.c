#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fs/operations.h"
#define MAX_INPUT_SIZE 100

int numberThreads = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void errorParse(){
    perror("Error: command invalid");
    exit(EXIT_FAILURE);
}

int applyCommands(char* command){
        
    int res;
    
    if (command == NULL){
        return FAIL;
    }  
    
    if ( pthread_mutex_lock(&mutex) != SUCCESS ) {
        perror("Error: failed to lock");
        exit(EXIT_FAILURE);
    } 
    char token;
    char name[MAX_INPUT_SIZE];
    char arg2[MAX_INPUT_SIZE];
    int numTokens;
    if ( (numTokens = sscanf(command, "%c %s %s", &token, name, arg2)) == EOF ) {
        perror("Error: sscanf fail");
        exit(EXIT_FAILURE);
    }
    if ( pthread_mutex_unlock(&mutex) != SUCCESS ) {
        perror("Error: failed to unlock");
        exit(EXIT_FAILURE);
    } 
    
    if (numTokens < 2) {
        perror("Error: invalid command in Queue");
        exit(EXIT_FAILURE);
    }  
    switch (token) {
        case 'c':
            switch (arg2[0]) {
                case 'f':
                    printf("Create file: %s\n", name);
                    res = create(name, T_FILE);
                    break;
                case 'd':
                    printf("Create directory: %s\n", name);
                    res = create(name, T_DIRECTORY);
                    break;
                default:
                    perror("Error: invalid node type");
                    exit(EXIT_FAILURE);
            }
            break;
        case 'l': 
            res = lookup(name);
            if (res >= 0)
                printf("Search: %s found\n", name);
            else
                printf("Search: %s not found\n", name);
            break;
        case 'd':
            printf("Delete: %s\n", name);
            res = delete(name);
            break;
        case 'm':
            printf("Move: %s to %s\n", name, arg2);
            res = move(name, arg2);
            break;
        case 'p':
            printf("Print: %s\n", name);
            res = print_tecnicofs_tree(name);
            break;
        default: { /* error */
            perror("Error: command to apply");
            exit(EXIT_FAILURE);
        }
    }
    return res;
}

int setSockAddrUn(char *path, struct sockaddr_un *addr) {

    if (addr == NULL)
        return 0;

    bzero((char *)addr, sizeof(struct sockaddr_un));
    addr->sun_family = AF_UNIX;
    strcpy(addr->sun_path, path);

    return SUN_LEN(addr);
}

void* threadfn(void* sockfd_aux){
    
    int sockfd = *((int*)sockfd_aux);
    struct sockaddr_un client_addr;
    socklen_t addrlen;
    char in_buffer[MAX_INPUT_SIZE], out_buffer[MAX_INPUT_SIZE];
    int c;


    while (1) {
        addrlen = sizeof(struct sockaddr_un);
        
        // Receive command from client
        if ( (c = recvfrom(sockfd, in_buffer, sizeof(in_buffer)-1, 0,
	                 (struct sockaddr *) &client_addr, &addrlen)) <= 0 ) {
            perror("Server: failed to receive");
            exit(EXIT_FAILURE);
        }
        in_buffer[c]='\0';
    
        int res = applyCommands(in_buffer);
    
        // int to string
        if ( sprintf(out_buffer, "%d", res) < 0 ) {
            perror("Server: sprintf fail");
            exit(EXIT_FAILURE);
        }
    
        // Send message to client
        if ( sendto(sockfd, out_buffer, c+1, 0,
                    (struct sockaddr *) &client_addr, addrlen) <= 0 ) {
            perror("Server: failed to send");
            exit(EXIT_FAILURE);
        }
    }

    pthread_exit(NULL);
}

void threadPool_init(pthread_t tid[], int sockfd) {

    // error check
    if ( numberThreads <= 0 ) {
        perror("Error: number of threads is zero.");
        exit(EXIT_FAILURE);
    }

    // starts all threads
    for ( int i = 0; i < numberThreads; i++ ) {
        if ( pthread_create(&tid[i], NULL, threadfn, (void*)&sockfd) != SUCCESS ) {
            perror("Error: failed to create a thread");
            exit(EXIT_FAILURE);
        }
    }
}

void threadPool_destroy(pthread_t tid[]) {
    
    for ( int i = 0; i < numberThreads; i++ ) {
        if ( pthread_join(tid[i], NULL) != SUCCESS ) {
            perror("Error: a thread failed.");
            exit(EXIT_FAILURE);
        }
    }

}

int main(int argc, char* argv[]) {
    
    int sockfd;
    struct sockaddr_un server_addr;
    socklen_t addrlen;
    char* path;


    if ( argc != 3 ) {
        perror("Error: argument count wrong");
        exit(EXIT_FAILURE);
    }

    /* init filesystem */
    init_fs();

    // Create Socket
    if ((sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("Server: can't open sock");
        exit(EXIT_FAILURE);
    }

    path = argv[2];

    // clean server address and reset it
    if ( unlink(path) < SUCCESS ) {
        perror("Server: couldn't unlink");
    }
    addrlen = setSockAddrUn (path, &server_addr);

    // bind
    if ( bind(sockfd, (struct sockaddr *) &server_addr, addrlen) < 0 ) {
        perror("Server: bind error");
        exit(EXIT_FAILURE);
    }

    if ( (numberThreads = atoi(argv[1])) == 0 ) {
        perror("Error: number of threads is 0");
        exit(EXIT_FAILURE);
    }
    pthread_t tid[numberThreads];

    threadPool_init(tid, sockfd);
    
    threadPool_destroy(tid);
    exit(EXIT_SUCCESS);
}
