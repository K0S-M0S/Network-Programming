#include "server_util.h"

int debug = 0; // 0 for regular use, 1 for debug mode
int interrupted = 0; // switches to 1 if interrupt is caught
int connections = 0;

/**
* Print instructions.
* @argc  number of arguments to main
* @argv  array of arguments to main
* Return 0 on sufficient number of arguments, 1 otherwise
*/
int usage(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Usage: %s [filename.job] [port]\n", argv[0]);
        printf("Debug: %s [filename.job] [port] -debug\n", argv[0]);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
  if(usage(argc, argv)) {
      return EXIT_SUCCESS;
  }

  if (argc == 4) {
    if (!strcmp(argv[3], "-debug"))
      debug = 1;
  }

  // set up signal handler
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handler;

  if (sigaction(SIGINT, &sa, NULL)) {
    perror(RED "[Client Error] Failed to catch interrupt signal" RESET);
    exit(EXIT_FAILURE);
  }

  if (debug) {
    printf(">>> %d <<< Server process start.\n", getpid());
    printf(">>> %d <<< Checking source file \"%s\".\n", getpid(), argv[1]);
  }
  FILE *job_file = fopen(argv[1], "r");
  if (job_file == NULL) {
    fprintf(stderr, ">>> %d <<< Failed to open file.\n", getpid());
    return EXIT_FAILURE;
  }
  fclose(job_file);

  if (debug) {
    printf(">>> %d <<< Creating socket for incoming connections.\n", getpid());
  }

  int sock = define_connection(argv[2]);
  if (sock == -1) {
    close(sock);
    return EXIT_FAILURE;
  }

  if (debug)
    printf(">>> %d <<< Opening source file \"%s\".\n", getpid(), argv[1]);
  job_file = fopen(argv[1], "r");
  int connection_status = accept_connections(sock, job_file);
  if (connection_status) {
    fprintf(stderr, ">>> %d <<< [Server Warning] Terminating due to an error.\n", getpid());
    fclose(job_file);
    close(sock);
    return EXIT_FAILURE;
  }
  printf(">>> %d <<< <Server Notification> Exiting program.\n", getpid());
  fclose(job_file);
  close(sock);

  return EXIT_SUCCESS;
}


/*======================== CONNECTION SETUP METHODS =========================*/

/**
* Create and prepare socket for connections.
* @port_string   port to connect to
* Return socket file descriptor on success, -1 otherwise.
*/
int define_connection(char *port_string) {
  struct sockaddr_in serveraddr;

  int port_int = parse_number(port_string);
  if (port_int == -1) {
    perror(RED "[Client Error] Failed to parse port argument" RESET);
    return -1;
  }
  unsigned short port = (unsigned short) port_int;

  int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    perror(RED "[Server Error] Could not create socket" RESET);
    return -1;
  }

  int enable = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) {
    perror(RED "[Server Error] Failed to change socket properties" RESET);
    return -1;
  }

  prepare_address(&serveraddr, port);
  int bind_status = bind(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (bind_status == -1) {
    perror(RED "[Server Error] Failed to assign address to socket" RESET);
    return -1;
  }

  int listen_status = listen(sock, SOMAXCONN);
  if (listen_status == -1) {
    perror(RED "[Server Error] Failed to prepare socket for connections" RESET);
    return -1;
  }
  return sock;
}

/**
* Prepare address struct (utility method).
* @serveraddr   address struct to prepare
* @port         port for clients to connect to
*/
void prepare_address(struct sockaddr_in *serveraddr, int port) {
  serveraddr->sin_family = AF_INET;
  serveraddr->sin_addr.s_addr = INADDR_ANY;
  serveraddr->sin_port = htons(port);
  memset(&(serveraddr->sin_zero), '\0', 8);
}

/**
* Accept connections from client and start processing queries.
* @sock     connection socket
* @fileptr  file to read jobs from
* Return -1 on error, 0 on success.
*/
int accept_connections(int sock, FILE *fileptr) {
  int client_sock = approve_connection(sock, 0);
  if (client_sock == -1)
    return -1;
  else if (client_sock == -2)
    return 0;

  if (set_nonblock(sock)) // make socket nonblocking for all new connections
    return -1;

  while(1) {
    int request_status = process_request(client_sock, fileptr);
    if (request_status == -1) {
      close(client_sock);
      return -1;
    } else if (request_status == 1) {
      break;
    }

    approve_connection(sock, 1);
  }
  return 0;
}

/**
* Set socket as nonblocking.
* @socket   make this socket nonblocking
* Return 0 on success, -1 on error.
*/
int set_nonblock(int socket) {
    int flags;
    flags = fcntl(socket,F_GETFL,0);
    if (flags == -1)
      return -1;
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    return 0;
}

/**
* Approve one connection and notify client of whether server is available.
* @sock       socket used to approve connection
* @nonblock   specify if socket is nonblocking
* Return ready socket on success, -1 on error, -2 on interrupt.
*/
int approve_connection(int sock, int nonblock) {
  struct sockaddr_in clientaddr;
  memset(&clientaddr, 0, sizeof(clientaddr));
  socklen_t clientaddrlen = sizeof(clientaddr);

  int client_sock;
  client_sock = accept(sock, (struct sockaddr *)&clientaddr, &clientaddrlen);
  if (!nonblock && client_sock == -1) {
    if (interrupted)
      return -2;
    perror(RED "[Server Error] Could not accept connection" RESET);
    return -1;
  }
  connections++;

  if (clientaddr.sin_addr.s_addr) {
    char *client_ip = inet_ntoa(clientaddr.sin_addr);
    printf(">>> %d <<< Client connected (address: %s).\n", getpid(), client_ip);

    ssize_t sent;
    unsigned char available;
    if (connections == 1) {
      if (debug)
        printf(">>> %d <<< Notifying client of server's availability.\n", getpid());
      available = 0;
      sent = write(client_sock, &available, sizeof(char));
    } else if (connections > 1) {
      if (debug)
        printf(">>> %d <<< Notifying client that server is busy.\n", getpid());
      available = (unsigned char) STOP_REQUEST;
      sent = write(client_sock, &available, sizeof(char));
      close(client_sock);
      connections--;
    }
    if (sent != sizeof(char)) {
      fprintf(stderr, RED ">>> %d <<< [Server Error] Failed to send notification to client.\n" RESET, getpid());
      return -1;
    }
    return client_sock;
  }
  return -1;
}


/*========================== COMMUNICATION METHODS ===========================*/

/**
* Process one query from client.
* Note: requests are processed in nonblocking mode so as to be able
*       to immediately notify other clients that server is busy.
* @client_sock   send reply via this socket
* @fileptr       file to read jobs from
* Return -1 on error, 0 on success, 1 on success and exit.
*/
int process_request(int client_sock, FILE *fileptr) {
  unsigned char request_char;
  ssize_t received = recv(client_sock, &request_char, sizeof(char), MSG_DONTWAIT);
  unsigned int request = (unsigned int) request_char;
  if (!request || received == -1) {
    if (interrupted) {
      send_message(client_sock, NULL);
      return 1;
    }
    return 0;
  }

  if (debug)
    printf("\n>>> %d <<< Received request (%d) from client.\n", getpid(), request);

  if (request == ONE_JOB_REQUEST) {
    return send_message(client_sock, fileptr);

  } else if (request < ALL_JOBS_REQUEST) {
    int num_jobs = request & 127;
    for (int i = 0; i < num_jobs; i++) {
      int send_status = send_message(client_sock, fileptr);
      if (send_status)
        return send_status;
    }
    return 0;

  } else if (request == ALL_JOBS_REQUEST) {
    while(1) {
      int send_status = send_message(client_sock, fileptr);
      if (send_status)
        return 0;
    }
    return 0;

  } else if (request == STOP_REQUEST) {
    printf(">>> %d <<< <Server Notification> Client disconnected.\n", getpid());
    return 1;

  } else if (request > STOP_REQUEST) {
    fprintf(stderr, ">>> %d <<< <Server Notification> Client disconnected with an error.\n", getpid());
    return 1;

  } else {
    fprintf(stderr, RED ">>> %d <<< [Server Error] Failed to process request: request type unknown.\n" RESET, getpid());
    return -1;
  }
}


/*====================== FILE READING AND JOB CREATION =======================*/

/**
* Send one message to client.
* @client_sock    send message via this socket
* @fileptr file   to read jobs from
* Return 1 if message text is empty, 0 otherwise.
*/
int send_message(int client_sock, FILE *fileptr) {
  struct JobMessage *msg = fetch_job(fileptr);
  int text_length = (msg->text_length == 0) ? 0 : ntohl(msg->text_length) + 1;
  ssize_t msg_size = sizeof(char) + sizeof(int) + sizeof(char) * text_length;
  if (debug)
    printf(">>> %d <<< Sending message (%li bytes) to client.\n", getpid(), msg_size);
  ssize_t sent_bytes = 0;
  while (sent_bytes < msg_size) {
    sent_bytes += write(client_sock, msg + sent_bytes, msg_size - sent_bytes);
  }
  micro_sleep(500);
  free(msg);
  if (!text_length)
    return 1;
  return 0;
}

/**
* Read file and put together a job for client.
* @fileptr  file to read jobs from
* Return job structure (type Q job on error/EOF).
*/
struct JobMessage *fetch_job(FILE *file_ptr) {
  if (!file_ptr) {
    struct JobMessage *quit_msg = create_msg((unsigned char) TYPE_Q, 0, NULL);
    return quit_msg;
  }

  if (debug)
    printf("\n>>> %d <<< Reading from file.\n", getpid());
  unsigned char job_type = fgetc(file_ptr);
  if (job_type == 'O')
    job_type = (unsigned char) TYPE_O;
  else if (job_type == 'E')
    job_type = (unsigned char) TYPE_E;
  else
    job_type = 'U'; // Unknown type

  unsigned int text_length = 0;
  for (int i = 0; i < 4; i++) {
    // unaffected by endianness due to bit shifting
    text_length += ((unsigned int) fgetc(file_ptr) << 8*i);
  }

  // maximum text length specified in "genjob.c" is 54,378 symbols
  if (!feof(file_ptr) && (text_length > 54378 || job_type == 'U')) {
    printf("Len: %d\n", text_length);
    printf("Type: %c\n", job_type);
    fprintf(stderr, ">>> %d <<< Invalid job encountered in file.\n", getpid());
    struct JobMessage *quit_msg = create_msg((unsigned char) TYPE_Q, 0, NULL);
    return quit_msg;
  }

  if (!feof(file_ptr)) {
    char *job_text = (char *) malloc(text_length+1);
    fread(job_text, sizeof(char), text_length, file_ptr);
    job_text[text_length] = '\0';
    struct JobMessage *msg = create_msg(job_type, text_length, job_text);
    return msg;

  } else {
    if (debug)
      printf(">>> %d <<< EOF encountered when reading file.\n", getpid());
    struct JobMessage *quit_msg = create_msg((unsigned char) TYPE_Q, 0, NULL);
    return quit_msg;
  }
}

/**
* Create job structure
* @job_type     type of job to create (can be 'O', 'E' or 'Q')
* @text_length  length of job text
* @job_text     text of job to create
* Return job structure.
*/
struct JobMessage *create_msg(unsigned char job_type, unsigned int text_length, char* job_text) {
  size_t msg_size;
  if (text_length)
    msg_size = sizeof(char) + sizeof(int) + sizeof(char) * (text_length + 1);
  else
    msg_size = sizeof(char) + sizeof(int);
  if (debug)
    printf(">>> %d <<< Allocating memory (%li bytes) for job structure.\n", getpid(), msg_size);
  struct JobMessage *msg = (struct JobMessage *) malloc(msg_size);
  msg->text_length = htonl(text_length);
  if (text_length)
    strncpy(msg->job_text, job_text, text_length+1);
  unsigned char job_info = (job_type << 5);
  if (job_type == (char) TYPE_O || job_type == (char) TYPE_E)
    job_info += checksum(job_text);
  msg->job_info = job_info;
  free(job_text);
  return msg;
}


/*====================== MISCELLANIOUS UTILITY METHODS =======================*/

/**
* Parse a positive integer in string form.
* @number_string  number to parse in string form
* Return parsed number on sucess, -1 on failure.
*/
int parse_number(char *number_string) {
  char *endptr;
  int result = strtol(number_string, &endptr, 10);
  if (endptr == number_string && result == 0)
    result = -1;
  return result;
}

/**
* Compute  checksum (Rule: sum of all characters in text % 32)
* @text    compute checksum of this text
* Return checksum as unsigned char.
*/
unsigned char checksum(char *text) {
  if (strlen(text)) {
    unsigned int sum = 0;
    for (unsigned int i = 0; i < strlen(text); i++)
      sum += text[i];
    unsigned char checksum = (unsigned char) (sum % 32);
    return checksum;
  } else {
    return 0;
  }
}

/**
* Suspend process for a time.
* @microseconds  suspend for this many microseconds
* Return 0 on success, -1 on failure.
*/
int micro_sleep(unsigned long microseconds) {
  struct timespec req, rem;
  time_t seconds = (int) (microseconds/1000000);
  time_t nanoseconds = microseconds * 1000;
  req.tv_sec = seconds;
  req.tv_nsec = nanoseconds;
  int sleep_status = nanosleep(&req, &rem);
  if (sleep_status && !interrupted)
    fprintf(stderr, ">>> %d <<< [Server Warning] Failed to sleep for %li ms.\n", getpid(), microseconds);
  return sleep_status;
}

/**
* Signal handler for the parent process.
* @signum  signal number
*/
void handler(int signum) {
  if (signum == SIGINT) {
    printf(">>> %d <<< Received interrupt signal.\n", getpid());
    interrupted = 1;
  }
}
