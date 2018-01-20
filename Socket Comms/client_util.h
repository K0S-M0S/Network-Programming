#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>

/* Brief request protocol description:
   Request type: unsigned char, 1 byte (8 bits).
   Request value:
     If 0 - 126, this many jobs are requested.
     If 127, all jobs are requested.
     If 128, normal termination.
     If 129 - 255, termination with error. */

#define ONE_JOB_REQUEST 1
#define ALL_JOBS_REQUEST 127
#define STOP_REQUEST 128
#define ERROR_REQUEST 129 // or any other value between 129 and 255

#define TYPE_O 0 // "000" bit pattern
#define TYPE_E 1 // "001" bit pattern
#define TYPE_Q 7 // "111" bit pattern

#define RED "\x1B[31m"
#define GRN   "\x1B[32m"
#define BLU   "\x1B[34m"
#define RESET "\x1B[0m"

struct JobMessage {
  unsigned char job_info;
  int text_length;
  char job_text[];
} __attribute__((packed));

int usage(int argc, char* argv[]);
int parse_number(char *number_string);
int prepare_address(struct sockaddr_in *serveraddr, char *ip_addr, int port);
int establish_connection(char *ip_addr, char *port_string);
int validate_checksum(struct JobMessage *msg);
int send_request(int socket, unsigned char request);
int send_to_pipe(int pipefd[2], unsigned char pipe_request, struct JobMessage *msg);
int receive_on_pipe(int pipefd[2], FILE *std_pointer);
int process_reply(int socket, int pipe_out[2], int pipe_err[2]);
int command_menu(int socket, int pipe_out[2], int pipe_err[2]);
int micro_sleep(unsigned long microseconds);
void handler(int signum);
