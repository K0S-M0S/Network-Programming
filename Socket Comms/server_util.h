#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>

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

#define RED   "\x1B[31m"
#define RESET "\x1B[0m"

struct JobMessage {
  unsigned char job_info;
  int text_length;
  char job_text[];
} __attribute__((packed));

int usage(int argc, char* argv[]);
int parse_number(char *number_string);
unsigned char checksum(char *text);
struct JobMessage *create_msg(unsigned char job_type, unsigned int text_length, char* job_text);
struct JobMessage *fetch_job(FILE *file_ptr);
void prepare_address(struct sockaddr_in *serveraddr, int port);
int define_connection(char *port_string);
int send_message(int client_sock, FILE *fileptr);
int process_request(int client_sock, FILE *fileptr);
int accept_connections(int sock, FILE *fileptr);
int approve_connection(int socket, int nonblock);
int set_nonblock(int socket);
int micro_sleep(unsigned long milliseconds);
void handler(int signum);
