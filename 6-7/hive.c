#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdio.h>      /* for printf() and fprintf() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <unistd.h>     /* for close() */

#define RCVBUFSIZE 2000
#define BEEHIVE_SIZE 30
const int HALF_BEEHIVE = BEEHIVE_SIZE / 2;
int pid = 0;
int sock; /* Socket descriptor */

typedef struct {
  sem_t bees;
  int honey_portions;
} HiveData;

HiveData *hive_data;

// имя области разделяемой памяти
const char *shar_object = "/posix-shar-object";

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

  // Изначально все n пчел находятся в улье, улететь могут (n-1) из них
  if (sem_init(&hive_data->bees, 0, n - 1) == -1) {
    perror("sem_init: Can not create bees semaphore");
    exit(-1);
  };
}

// Функция, закрывающая семафоры и разделяемую память
void my_close(void) {
  if (sem_destroy(&hive_data->bees) == -1) {
    perror("sem_destroy: Incorrect destruction of bees semaphore");
    exit(-1);
  };
}

// Функция, удаляющая азделяемую память
void my_unlink(void) {
  if (shm_unlink(shar_object) == -1) {
    perror("shm_unlink: shared memory");
    exit(-1);
  }
}

int my_rand(int min, int max) {
  srand(time(NULL));
  return rand() % (max - min + 1) + min;
}

void DieWithError(char *errorMessage) {
  perror(errorMessage);
  exit(1);
}

void sigint_handler(int signum) {
  my_close();
  if (pid != 0) {
    my_unlink();
    printf("You've stopped the program. The bees are grateful for saving them "
           "from the bear!\n");

    if (send(sock, "", 0, 0) != 0)
      DieWithError("send() sent a different number of bytes than expected");
    kill(0, SIGKILL); // Send SIGKILL signal to all child processes
  }
  exit(signum);
}

int main(int argc, char *argv[]) {
  struct sockaddr_in servAddr;   /* Echo server address */
  unsigned short servPort;       /* Echo server port */
  char *servIP;                  /* Server IP address (dotted quad) */
  char buffer[RCVBUFSIZE];       /* Buffer for echo string */
  unsigned int mesLen;           /* Length of string to echo */
  int bytesRcvd, totalBytesRcvd; /* Bytes read in single recv()
                                    and total bytes read */
  int n;                         /* Number of bees */

  if ((argc < 3) || (argc > 4)) /* Test for correct number of arguments */
  {
    fprintf(
        stderr,
        "Usage: %s <Server IP> <Number of bees greater than 3> [<Echo Port>]\n",
        argv[0]);
    exit(1);
  }

  servIP = argv[1]; /* First arg: server IP address (dotted quad) */
  n = atoi(argv[2]);
  if (n <= 3) {
    printf("Expected a number greater than 3.\n");
    exit(-1);
  }

  signal(SIGINT, sigint_handler);
  my_init(n);

  if (argc == 4)
    servPort = atoi(argv[3]); /* Use given port, if any */
  else
    servPort = 7; /* 7 is the well-known port for the echo service */

  printf("%d\n", servPort);

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

  printf("connected!\n");

  // Создание подпроцессов пчел
  for (int i = 0; i < n; i++) {
    if ((pid = fork()) > 0)
      continue; // процесс-родитель

    // внутри подпроцесса пчелы
    printf("Bee %d is born!\n", i);

    while (1) {
      int bees_in_hive = 0;
      sem_wait(&hive_data->bees);
      sem_getvalue(&hive_data->bees, &bees_in_hive);
      ++bees_in_hive;
      printf("Bee %d is leaving the hive. There are %d bees left there.\n",
             i + 1, bees_in_hive);
      sleep(my_rand(1, 4)); // Искать мед - сложная работа!

      // пчела кладет мед в улей
      if (hive_data->honey_portions < BEEHIVE_SIZE) {
        ++(hive_data->honey_portions);
        printf("Bee %d is putting honey in the hive.\n", i + 1);
      } else {
        printf("The hive is full!\n");
      }

      sem_post(&hive_data->bees);
      sem_getvalue(&hive_data->bees, &bees_in_hive);
      ++bees_in_hive;
      printf("Bee %d stays in the hive for a while. There are %d bees in hive "
             "now.\n",
             i + 1, bees_in_hive);
      sleep(my_rand(1, 3));
    }
  }

  if ((pid = fork()) > 0) {
    while (1) {
      sleep(5); // обновляем информацию раз в 5 секунд
      int bees_in_hive = 0;
      sem_getvalue(&hive_data->bees, &bees_in_hive);
      ++bees_in_hive;

      char mes[RCVBUFSIZE] = "";
      sprintf(mes, "%d %d\n", bees_in_hive, hive_data->honey_portions);
      mesLen = strlen(mes);
      /* Send the string to the server */
      if (send(sock, mes, mesLen, 0) != mesLen)
        DieWithError("send() sent a different number of bytes than expected");
      printf("Update sent to server: %s", mes);
    }
  }

  while (1) {
    if ((bytesRcvd = recv(sock, buffer, RCVBUFSIZE - 1, 0)) <= 0)
      DieWithError("recv() failed or connection closed prematurely");
    if (buffer[0] == '0') 
      sigint_handler(1);
    if (buffer[0] == '1') {
      printf("Oh no! The honey is stolen!\n");
      hive_data->honey_portions = 0;
    }
  }

  close(sock);
  exit(0);
}