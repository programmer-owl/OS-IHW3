#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdio.h>      /* for printf() and fprintf() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <sys/socket.h> /* for socket(), bind(), and connect() */
#include <unistd.h>     /* for close() */

#define RCVBUFSIZE 500
#define MAXPENDING 7
#define BEEHIVE_SIZE 30
const int HALF_BEEHIVE = BEEHIVE_SIZE / 2;
int n = 0;
int bearSock, beeSock; /* Socket descriptors for bear and beehive */
int pid = 0;

typedef struct {
  int bees;
  int honey_portions;
} HiveData;

HiveData *hive_data;

// имя области разделяемой памяти
const char *shar_object = "/posix-shar-object-1";

// Функция, осуществляющая при запуске манипуляции с памятью и семафорами
void my_init(int n) {
  int shmid = shm_open(shar_object, O_CREAT | O_RDWR, 0666);
  if (shmid == -1) {
    perror("shm_open");
    exit(-1);
  }
  // Задание размера объекта памяти
  if (ftruncate(shmid, sizeof(HiveData)) == -1) {
    perror("ftruncate: memory sizing error");
    exit(-1);
  }
  hive_data =
      mmap(0, sizeof(HiveData), PROT_READ | PROT_WRITE, MAP_SHARED, shmid, 0);
  if (hive_data == MAP_FAILED) {
    perror("mmap");
    exit(-1);
  }
  hive_data->honey_portions = 0;
}

// Функция, удаляющая разделяемую память
void my_unlink(void) {
  if (shm_unlink(shar_object) == -1) {
    perror("shm_unlink: shared memory");
    exit(-1);
  }
}

void DieWithError(char *errorMessage) {
  perror(errorMessage);
  exit(1);
}

void sigint_handler(int signum) {
  if (pid == 0) {
    my_unlink();
    printf("You've stopped the program. The bees are grateful for saving them "
           "from the bear!\n");
  }
  if (send(bearSock, "0", 1, 0) != 1)
    DieWithError("send() failed");
  if (send(beeSock, "0", 1, 0) != 1)
    DieWithError("send() failed");
  close(beeSock); /* Close client socket */
  close(bearSock);
  if (pid == 0)  
    kill(0, SIGKILL); // Send SIGKILL signal to all child processes

  exit(signum);
}

void HandleBearClient() {
  char buffer[RCVBUFSIZE]; /* Buffer for string */
  int recvMsgSize;         /* Size of received message */

  while (1) {
    /* Receive message from client */
    if ((recvMsgSize = recv(bearSock, buffer, RCVBUFSIZE, 0)) < 0)
      DieWithError("recv() failed");
    if (recvMsgSize == 0) 
      break;
    printf("Received question from bear.\n");

    if (hive_data->honey_portions < HALF_BEEHIVE)
      sprintf(buffer,
              "There are %d portions of honey in the hive. Winnie is not "
              "interested!\n",
              hive_data->honey_portions);
    else if (hive_data->bees >= 3)
      sprintf(buffer, "Ow! There are %d bees in the hive. Winnie got stung!\n",
              hive_data->bees);
    else {
      sprintf(buffer, "Success! Winnie stole the honey!\n");
      if (send(beeSock, "1", 1, 0) != 1)
        // сообщаем улью, что мед украли
        DieWithError("send() failed");
    }
    printf("%s", buffer);
    send(bearSock, buffer, RCVBUFSIZE, 0);
  }

  printf("Bear is done with this.\n");
}

void HandleBeehiveClient() {
  char echoBuffer[RCVBUFSIZE]; /* Buffer for echo string */
  int recvMsgSize;             /* Size of received message */

  while (1) {
    if ((recvMsgSize = recv(beeSock, echoBuffer, RCVBUFSIZE, 0)) < 0)
      DieWithError("recv() failed");
    if (recvMsgSize == 0) 
      break;
    printf("Received info from the hive: %s", echoBuffer);
    char new_bih[RCVBUFSIZE];
    char new_hp[RCVBUFSIZE];
    int flag = 0;
    int j1 = 0, j2 = 0;
    for (int i = 0; i < recvMsgSize - 1; i++) {
      if (echoBuffer[i] == ' ')
        flag = 1;
      else if (flag == 0) {
        new_bih[j1] = echoBuffer[i];
        j1 += 1;
      } else {
        new_hp[j2] = echoBuffer[i];
        j2 += 1;
      }
    }
    hive_data->bees = atoi(new_bih);
    hive_data->honey_portions = atoi(new_hp);
  }

  printf("Bees are done with this.\n");
}

int CreateTCPServerSocket(unsigned short port) {
  int sock;                        /* socket to create */
  struct sockaddr_in echoServAddr; /* Local address */

  /* Create socket for incoming connections */
  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    DieWithError("socket() failed");

  /* Construct local address structure */
  memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
  echoServAddr.sin_family = AF_INET;              /* Internet address family */
  echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
  echoServAddr.sin_port = htons(port);              /* Local port */

  /* Bind to the local address */
  if (bind(sock, (struct sockaddr *)&echoServAddr, sizeof(echoServAddr)) < 0)
    DieWithError("bind() failed");

  /* Mark the socket so it will listen for incoming connections */
  if (listen(sock, MAXPENDING) < 0)
    DieWithError("listen() failed");

  return sock;
}

int AcceptTCPConnection(int servSock) {
  int clntSock;                    /* Socket descriptor for client */
  struct sockaddr_in echoClntAddr; /* Client address */
  unsigned int clntLen;            /* Length of client address data structure */

  /* Set the size of the in-out parameter */
  clntLen = sizeof(echoClntAddr);

  /* Wait for a client to connect */
  if ((clntSock =
           accept(servSock, (struct sockaddr *)&echoClntAddr, &clntLen)) < 0)
    DieWithError("accept() failed");

  /* clntSock is connected to a client! */

  printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));

  return clntSock;
}

int main(int argc, char *argv[]) {
  int servSock;                    /* Socket descriptor for server */
  unsigned short servPort;         /* Server port */
  unsigned int childProcCount = 0; /* Number of child processes */

  if (argc != 3) /* Test for correct number of arguments */
  {
    fprintf(stderr,
            "Usage:  %s <Number of bees greater than 3> <Server Port>\n",
            argv[0]);
    exit(1);
  }

  n = atoi(argv[1]);
  if (n <= 3) {
    printf("Expected a number greater than 3.\n");
    exit(-1);
  }

  signal(SIGINT, sigint_handler);
  my_init(n);

  servPort = atoi(argv[2]); /* local port */

  servSock = CreateTCPServerSocket(servPort);
  printf("serv\n");
  beeSock = AcceptTCPConnection(servSock);
  printf("bee\n");
  bearSock = AcceptTCPConnection(servSock);
  printf("bear\n");

  /* Fork a child process for each client */
  pid = fork();
  if (pid == 0) {
    /* beehive process */
    HandleBeehiveClient();
    exit(0);
  }

  pid = fork();
  if (pid == 0) {
    /* bear process */
    HandleBearClient();
    exit(0);
  }

  /* Parent process is resting */
  while (1) {
  };
}
