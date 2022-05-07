#include "tecnicofs-client-api.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

int sockfd; // client's sockfd

char* serverName;
socklen_t servlen;
struct sockaddr_un serv_addr;
char* client_path;


int setSockAddrUn(char* path, struct sockaddr_un* addr) {

  if (addr == NULL)
    return 0;

  bzero((char *)addr, sizeof(struct sockaddr_un));
  addr->sun_family = AF_UNIX;
  strcpy(addr->sun_path, path);

  return SUN_LEN(addr);
}

int setSockAddrUnClient(struct sockaddr_un* addr) {

  if (addr == NULL)
    return 0;

  bzero((char *)addr, sizeof(struct sockaddr_un));
  addr->sun_family = AF_UNIX;
  client_path = tmpnam(addr->sun_path);
  return SUN_LEN(addr);
}


int tfsCreate(char* filename, char nodeType) {
  char command[MAX_INPUT_SIZE], res_str[MAX_INPUT_SIZE];
  int c;

  if ( sprintf(command, "c %s %c", filename, nodeType) < 0 ) {
    perror("Client Create: sprintf failed");
    exit(EXIT_FAILURE);
  }

  // send
  if (sendto(sockfd, command, strlen(command)+1, 0,
             (struct sockaddr *) &serv_addr, servlen) < 0) {
    perror("Client Create: sendto error");
    exit(EXIT_FAILURE);
  }

  // receive
  if ((c = recvfrom(sockfd, res_str, sizeof(res_str)-1, 0, 0,0)) < 0) {

    perror("Client Create: recvfrom error");
    exit(EXIT_FAILURE);
  }
  res_str[c]='\0';
  return atoi(res_str);
}

int tfsDelete(char* path) {
  char command[MAX_INPUT_SIZE], res_str[MAX_INPUT_SIZE];
  int c;

  if ( sprintf(command, "d %s", path) < 0 ) {
    perror("Client Delete: sprintf failed");
    exit(EXIT_FAILURE);
  }

  // send
  if (sendto(sockfd, command, strlen(command)+1, 0,
             (struct sockaddr *) &serv_addr, servlen) < 0) {
    perror("Client Delete: sendto error");
    exit(EXIT_FAILURE);
  }

  // receive
  if ((c = recvfrom(sockfd, res_str, sizeof(res_str)-1, 0,0,0)) < 0) {
    perror("Client Delete: recvfrom error");
    exit(EXIT_FAILURE);
  }
  res_str[c]='\0';
  return atoi(res_str);
}

int tfsMove(char* from, char* to) {
  char command[MAX_INPUT_SIZE], res_str[MAX_INPUT_SIZE];
  int c;
  
  if ( sprintf(command, "m %s %s",from,to) < 0 ) {
    perror("Client Move: sprintf failed");
    exit(EXIT_FAILURE);
  }

  // send
  if (sendto(sockfd, command, strlen(command)+1, 0,
             (struct sockaddr *) &serv_addr, servlen) < 0) {
    perror("Client Move: sendto error");
    exit(EXIT_FAILURE);
  }

  // receive
  if ((c = recvfrom(sockfd, res_str, sizeof(res_str)-1, 0,0,0)) < 0) {
    perror("Client Move: recvfrom error");
    exit(EXIT_FAILURE);
  }
  res_str[c]='\0';
  return atoi(res_str);
}

int tfsPrint(char* outputfile) {
  char command[MAX_INPUT_SIZE], res_str[MAX_INPUT_SIZE];
  int c;
  
  if ( sprintf(command, "p %s", outputfile) < 0 ) {
    perror("Client Print: sprintf failed");
    exit(EXIT_FAILURE);
  }

  // send
  if (sendto(sockfd, command, strlen(command)+1, 0,
             (struct sockaddr *) &serv_addr, servlen) < 0) {
    perror("Client Print: sendto error");
    exit(EXIT_FAILURE);
  }

  // receive
  if ((c = recvfrom(sockfd, res_str, sizeof(res_str)-1, 0, 0, 0)) < 0) {
    perror("Client Print: recvfrom error");
    exit(EXIT_FAILURE);
  }
  res_str[c]='\0';
  return atoi(res_str);
}

int tfsLookup(char* path) {
  char command[MAX_INPUT_SIZE], res_str[MAX_INPUT_SIZE];
  int c;
  
  if ( sprintf(command, "l %s",path) < 0 ) {
    perror("Client Lookup: sprintf failed");
    exit(EXIT_FAILURE);
  }

  // send
  if (sendto(sockfd, command, strlen(command)+1, 0,
             (struct sockaddr *) &serv_addr, servlen) < 0) {
    perror("Client Lookup: sendto error");
    exit(EXIT_FAILURE);
  }

  // receive
  if ((c = recvfrom(sockfd, res_str, sizeof(res_str)-1, 0,0,0)) < 0) {
    perror("Client Lookup: recvfrom error");
    exit(EXIT_FAILURE);
  }
  res_str[c]='\0';
  return atoi(res_str);
}

int tfsMount(char* sockPath) {
  
  socklen_t clilen;
  struct sockaddr_un client_addr;

  // create client's socket
  if ( (sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0 ) {
    perror("Client: can't open socket");
    exit(EXIT_FAILURE);
  }

  // clean client's attributes and reset them
  unlink(client_path);
  clilen = setSockAddrUnClient(&client_addr);

  // bind
  if ( bind(sockfd, (struct sockaddr *) &client_addr, clilen) < 0 ) {
    perror("Client: bind error");
    exit(EXIT_FAILURE);
  }

  // take care of server's socket attributes
  strcpy(serverName,sockPath);
  servlen = setSockAddrUn(sockPath, &serv_addr);
  return 0;
}

int tfsUnmount() {
  if (close(sockfd) < 0)
  {
    perror("Client: couldn't close socket");
    exit(EXIT_FAILURE);
  }
  return 0;
}
