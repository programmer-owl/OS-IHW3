#include <signal.h>
#include <time.h>
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdio.h>      /* for printf() and fprintf() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <unistd.h>     /* for close() */

#define RCVBUFSIZE 2000
int sock; /* Socket descriptor */

void DieWithError(char *errorMessage) {
  perror(errorMessage);
  exit(1);
}

void sigint_handler(int signum) {
  printf("Bye!\n");
  exit(signum);
}

int main(int argc, char *argv[]) {
  struct sockaddr_in servAddr;   /* Echo server address */
  unsigned short servPort;       /* Echo server port */
  char *servIP;                  /* Server IP address (dotted quad) */
  char *mes;                     /* String to send to echo server */
  char buffer[RCVBUFSIZE];       /* Buffer for echo string */
  unsigned int mesLen;           /* Length of string to echo */
  int bytesRcvd, totalBytesRcvd; /* Bytes read in single recv()
                                    and total bytes read */
  signal(SIGINT, sigint_handler);

  if ((argc < 2) || (argc > 3)) /* Test for correct number of arguments */
  {
    fprintf(stderr, "Usage: %s <Server IP> [<Echo Port>]\n", argv[0]);
    exit(1);
  }

  servIP = argv[1]; /* First arg: server IP address (dotted quad) */
  if (argc == 3)
    servPort = atoi(argv[2]); /* Use given port, if any */
  else
    servPort = 7; /* 7 is the well-known port for the echo service */

  /* Create a reliable, stream socket using TCP */
  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    DieWithError("socket() failed");

  /* Construct the server address structure */
  memset(&servAddr, 0, sizeof(servAddr));       /* Zero out structure */
  servAddr.sin_family = AF_INET;                /* Internet address family */
  servAddr.sin_addr.s_addr = inet_addr(servIP); /* Server IP address */
  servAddr.sin_port = htons(servPort);          /* Server port */

  /* Establish the connection to the server */
  if (connect(sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
    DieWithError("connect() failed");

  while (1) {
    if ((bytesRcvd = recv(sock, buffer, RCVBUFSIZE, 0)) <= 0)
      DieWithError("recv() failed or connection closed prematurely");
    if (buffer[0] == '0') sigint_handler(1);
    // print the answer from server
    printf("%s", buffer);
  }
  close(sock);
  exit(0);
}
