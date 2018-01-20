#include "client_util.h"

int debug = 0; // 0 for normal use, 1 for debug mode
int interrupted = 0; // switch to 1 when interrupt is caught

/**
* Print instructions.
* @argc  number of arguments to main
* @argv  array of arguments to main
* Return 1 on insufficient number of arguments, 0 otherwise.
*/
int usage(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Usage: %s [server address] [port]\n", argv[0]);
        printf("Debug: %s [server address] [port] -debug\n", argv[0]);
        printf("Server address is its domain name or IPv4 address (IPv6 is NOT supported).\n");
        printf("Note: The first IP address in the table is used on DNS lookup.\n");
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

  // signal handling
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));

  if (debug)
    printf(">>> %d <<< Client process start.\n", getpid());

  int sock = establish_connection(argv[1], argv[2]);
  if (sock < 0) {
    close(sock);
    if (sock == -2)
      return EXIT_SUCCESS;
    return EXIT_FAILURE;
  }

  if (debug)
    printf(">>> %d <<< Connected to address %s, port %s.\n", getpid(), argv[1], argv[2]);

  // pipe creation
  int pipe_out[2], pipe_err[2];
  if (pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
    perror(RED "[Client Error] Pipe creation failed" RESET);
    send_request(sock, ERROR_REQUEST);
    close(sock);
    return EXIT_FAILURE;
  }

  int client_pid = getpid();
  int out_pid;
  int err_pid;

  // begin forks
  if ((out_pid = fork())) { // parent
    if (debug)
      printf(">>> %d <<< New process (PID: %d) generated due to fork.\n", getpid(), out_pid);

    if ((err_pid = fork())) { // parent
      sa.sa_handler = handler;
      if (sigaction(SIGINT, &sa, NULL)) {
        perror(RED "[Client Error] Failed to catch interrupt signal" RESET);
        exit(EXIT_FAILURE);
      }

      if (debug)
        printf(">>> %d <<< New process (PID: %d) generated due to fork.\n", getpid(), err_pid);

      close(pipe_out[0]);
      close(pipe_err[0]);

      int menu_status = command_menu(sock, pipe_out, pipe_err);
      close(pipe_out[1]);
      close(pipe_err[1]);

      if (menu_status) {
        fprintf(stderr, ">>> %d <<< [Client Warning] Terminating due to an error.\n", getpid());
        send_request(sock, ERROR_REQUEST);
        close(sock);
        waitpid(0, NULL, 0);
        return EXIT_FAILURE;
      } else {
        printf(">>> %d <<< <Client Notification> Terminating process.\n", getpid());
        waitpid(0, NULL, 0);
        return EXIT_SUCCESS;
      }

    } else { // child 2 (stderr printer)
      sa.sa_handler = SIG_IGN;
      sigaction(SIGINT, &sa, NULL);

      if (debug)
        printf(">>> %d <<< Stderr printing process start (fork from %d).\n", getpid(), client_pid);

      close(pipe_out[0]);
      close(pipe_out[1]);
      close(pipe_err[1]);

      while (1) {
        int status = receive_on_pipe(pipe_err, stderr);
        if (status) {
          close(pipe_err[0]);
          if (status == 1) {
            close(sock);
            if (debug)
              printf(">>> %d <<< Stderr printing process terminated.\n", getpid());
            return EXIT_SUCCESS;
          }
          send_request(sock, ERROR_REQUEST);
          if (debug)
            printf(">>> %d <<< Stderr printing process terminated with an error.\n", getpid());
          return EXIT_FAILURE;
        }
      }
    }

  } else { // child 1 (stdout printer)
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);

    if (debug)
      printf(">>> %d <<< Stdout printing process start (fork from %d).\n", getpid(), client_pid);

    close(pipe_err[0]);
    close(pipe_err[1]);
    close(pipe_out[1]);
    while (1) {
      int status = receive_on_pipe(pipe_out, stdout);
      if (status) {
        close(pipe_out[0]);
        if (status == 1) {
          close(sock);
          if (debug)
            printf(">>> %d <<< Stdout printing process terminated.\n", getpid());
          return EXIT_SUCCESS;
        }
        send_request(sock, ERROR_REQUEST);
        if (debug)
          printf(">>> %d <<< Stdout printing process terminated with an error.\n", getpid());
        return EXIT_FAILURE;
      }
    }
  }
}

/**
* Connect to server.
* @ip_addr  server's IP address in string form
* @port     server's port in string form
* Return prepared socket on success, -1 on error.
*/
int establish_connection(char *host_addr, char *port_string) {
  if (debug)
    printf(">>> %d <<< Attempting to connect to address %s, port %s.\n", getpid(), host_addr, port_string);
  int sock;
  struct sockaddr_in serveraddr; //clientaddr;

  int port_int = parse_number(port_string);
  if (port_int == -1) {
    perror(RED "[Client Error] Failed to parse port argument" RESET);
    return -1;
  }
  unsigned short port = (unsigned short) port_int;

  if (debug)
    printf(">>> %d <<< Creating socket.\n", getpid());
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    perror(RED "[Client Error] Failed to create socket" RESET);
    return -1;
  }

  int ip_status = prepare_address(&serveraddr, host_addr, port);
  if (ip_status != 1) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to resolve server address.\n" RESET, getpid());
    return -1;
  }

  int connection_status = connect(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (connection_status == -1) {
    perror(RED "[Client Error] Failed to connect to server" RESET);
    return -1;
  }

  unsigned char available;
  ssize_t received;
  if (debug)
    printf(">>> %d <<< Confirming server's availability.\n", getpid());
  received = read(sock, &available, sizeof(char));
  if (received != sizeof(char)) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to confirm server's availability.\n" RESET, getpid());
    return -1;
  }

  if (available == 0) {
    printf(">>> %d <<< <Client Notification> Server is ready to accept connections.\n", getpid());
  } else {
    printf(">>> %d <<< <Client Notification> Server is busy.\n", getpid());
    return -2;
  }
  return sock;
}

/**
* Prepare address struct (utility method).
* @serveraddr  address struct to prepare
* @ip_addr     server's IP address
* @port        server's port
* Return 1 if IP address is resolved, 0 if failed to resolve.
*/
int prepare_address(struct sockaddr_in *serveraddr, char *host_addr, int port) {
  serveraddr->sin_family = AF_INET;
  int ip_status = inet_aton(host_addr, &(serveraddr->sin_addr));
  if (!ip_status) { // DNS lookup
    struct hostent *hostinfo;
    if ((hostinfo = gethostbyname(host_addr)) == NULL)
      return 0;
    ip_status = 1;
    serveraddr->sin_addr = *((struct in_addr *)hostinfo->h_addr_list[0]);
  }
  serveraddr->sin_port = htons(port);
  memset(&(serveraddr->sin_zero), '\0', 8);
  return ip_status;
}

/**
* Print command menu, process user input.
* @socket     send queries via this socket
* @pipe_out   send information to stdout printer via this pipe
* @pipe_err   send information to stderr printer via this pipe
* Return -1 on errors propagated from lower level methods, 0 on success.
*/
int command_menu(int socket, int pipe_out[2], int pipe_err[2]) {
  while (1) {
    micro_sleep(100000); // preserve printing order

    if (!interrupted) {
      printf(RESET "\nMENU:\n");
      printf("1) Fetch one job from the server\n");
      printf("2) Fetch several jobs from the server\n");
      printf("3) Fetch all jobs from the server\n");
      printf("4) Exit Program\n");
      printf("Enter Option (1-4): ");
    }
    int option;

    char option_buf[128];
    fgets(option_buf, sizeof(option_buf), stdin);
    if (interrupted) {
      option = 4;
    } else {
      option_buf[strcspn(option_buf, "\n")] = 0;
      option = atoi(option_buf);
      if (option < 0 || option > 4) {
        printf("Invalid input.\n");
        continue;
      }
    }
    printf("\n");

    if (option == 1) {
      if (debug)
        printf(">>> %d <<< Sending request (%d) to server.\n", getpid(), ONE_JOB_REQUEST);
      int request_status = send_request(socket, (char) ONE_JOB_REQUEST);
      if (request_status)
        return -1;
      int process_status = process_reply(socket, pipe_out, pipe_err);
      if (process_status <= 0)
        return process_status;

    } else if (option == 2) {
      printf("Enter the number of jobs to fetch (0 - 126): ");
      int jobs;

      char jobs_buf[128];
      fgets(jobs_buf, sizeof(jobs_buf), stdin);
      if (interrupted) {
        if (send_request(socket, (unsigned char) STOP_REQUEST))
          return -1;
        return 0;
      } else {
        jobs_buf[strcspn(jobs_buf, "\n")] = 0;
        jobs = atoi(jobs_buf);
        if (jobs < 0 || option > 126) {
          printf("Invalid input.\n");
          continue;
        }
      }

      unsigned char request = ((char) jobs) & 127;
      if (debug)
        printf(">>> %d <<< Sending request (%d) to server.\n", getpid(), (int) request);
      int request_status = send_request(socket, request);
      if (request_status)
        return -1;
      for (int i = 0; i < jobs; i++) {
        int process_status = process_reply(socket, pipe_out, pipe_err);
        if (process_status <= 0)
          return process_status;
      }

    } else if (option == 3) {
      if (debug)
        printf(">>> %d <<< Sending request (%d) to server.\n", getpid(), ALL_JOBS_REQUEST);
      int request_status = send_request(socket, (unsigned char) ALL_JOBS_REQUEST);
      if (request_status)
        return -1;

      while(1) {
        int process_status = process_reply(socket, pipe_out, pipe_err);
        if (process_status <= 0)
          return process_status;
      }

    } else if (option == 4){
      if (debug)
        printf(">>> %d <<< Sending request (%d) to server.\n", getpid(), STOP_REQUEST);
      unsigned char stop_request = (unsigned char) STOP_REQUEST;
      if (send_request(socket, stop_request))
        return -1;
      if (send_to_pipe(pipe_out, stop_request, NULL) == -1)
        return -1;
      if (send_to_pipe(pipe_err, stop_request, NULL) == -1)
        return -1;
      printf(">>> %d <<< <Client Notification> Disconnecting from the server.\n", getpid());
      return 0;

    } else {
      printf("Invalid option.\n");
    }
  }
  return 0;
}


/*==================== COMMUNICATION WITH SERVER AND PIPES ===================*/

/**
* Send one request to server.
* @socket    send request via this socket
* @request   send this request
* Return 0 on success, -1 on failure.
*/
int send_request(int socket, unsigned char request) {
  ssize_t sent = write(socket, &request, sizeof(char));
  if (sent != sizeof(char)) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to send request.\n" RESET, getpid());
    return -1;
  }
  return 0;
}

/**
* Process server's reply.
* @socket     read reply from this socket
* @pipe_out   send information to stdout printer via this pipe
* @pipe_err   send information to stderr printer via this pipe
* Return -1 on error, 0 on success and quit, 1 if received 'O' or 'E' type job.
*/
int process_reply(int socket, int pipe_out[2], int pipe_err[2]) {
  unsigned char job_info;
  if (read(socket, &job_info, sizeof(char)) != sizeof(char)) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to receive job information.\n" RESET, getpid());
    return -1;
  }

  // read text length
  int text_length;
  if (read(socket, &text_length, sizeof(int)) != sizeof(int)) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to receive job text length.\n" RESET, getpid());
    return -1;
  }
  text_length = ntohl(text_length);
  ssize_t text_size = (text_length == 0) ? 0 : sizeof(char) * (text_length+1);
  ssize_t msg_size = sizeof(char) + sizeof(int) + text_size;
  struct JobMessage *msg = (struct JobMessage *) malloc(msg_size);

  msg->job_info = job_info;
  msg->text_length = text_length;

  // read job text
  ssize_t received_bytes = 0;
  ssize_t received_currently = 0;
  while (received_bytes < text_size) {
    received_currently = read(socket, msg->job_text + received_bytes, text_size - received_bytes);
    if (received_currently == -1) {
      perror(RED "[Client Error] Failed to receive text" RESET);
      return -1;
    } else {
      received_bytes += received_currently;
    }
  }

  if (debug)
    printf("\n>>> %d <<< Received message (%li bytes) from server.\n", getpid(), msg_size);

  int validation = validate_checksum(msg);
  if (validation) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Checksum validation failed.\n" RESET, getpid());
    return -1;
  }

  unsigned char job_type = (msg->job_info) >> 5;
  if (job_type == (unsigned char) TYPE_O) {
    if (send_to_pipe(pipe_out, (unsigned char) ONE_JOB_REQUEST, msg) == -1)
      return -1;
    free(msg);
    return 1;

  } else if (job_type == (unsigned char) TYPE_E) {
    if (send_to_pipe(pipe_err, (unsigned char) ONE_JOB_REQUEST, msg) == -1)
      return -1;
    free(msg);
    return 1;

  } else if (job_type == (unsigned char) TYPE_Q) {
    free(msg);
    unsigned char request = (unsigned char) STOP_REQUEST;
    if (debug) {
      printf(">>> %d <<< Received type 'Q' job.\n", getpid());
      printf(">>> %d <<< Sending request (%d) to server and pipes.\n", getpid(), (int) request);
    }
    if (send_to_pipe(pipe_out, request, NULL) == -1)
      return -1;
    if (send_to_pipe(pipe_err, request, NULL) == -1)
      return -1;
    if (send_request(socket, request) == -1)
      return -1;
    micro_sleep(100);
    printf("\n>>> %d <<< <Client Notification> All jobs finished.\n", getpid());
    return 0;

  } else {
    free(msg);
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to process message: job type unknown.\n" RESET, getpid());
    return -1;
  }
}

/**
* Send message to another process via pipe.
* @pipefd         send via this pipe
* @pipe_request   request to send
* @msg            message to send
* Return -1 on error, 0 on success, 1 if sent termination request.
*/
int send_to_pipe(int pipefd[2], unsigned char pipe_request, struct JobMessage *msg) {
  if (msg) {
    msg->job_info = pipe_request; // place request as job info for compact sending
    ssize_t text_length = msg->text_length + 1;
    ssize_t msg_size = sizeof(char) + sizeof(int) + (sizeof(char) * text_length);
    ssize_t sent_bytes = 0;
    ssize_t sent_currently = 0;
    if (debug)
      printf(">>> %d <<< Sending message (%li bytes) to pipe.\n", getpid(), msg_size);
    while (sent_bytes < msg_size) {
      sent_currently = write(pipefd[1], msg + sent_bytes, msg_size - sent_bytes);
      if (sent_currently == -1) {
        perror(RED "[Client Error] Failed to send message to pipe" RESET);
        return -1;
      } else {
        sent_bytes += sent_currently;
      }
    }
    return 0;
  } else {
    if (write(pipefd[1], &pipe_request, sizeof(char)) != 1) {
      if (debug)
        printf(">>> %d <<< Sending request (%d) to pipe.\n", getpid(), (int) pipe_request);
      fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to send termination request to pipe.\n" RESET, getpid());
      return -1;
    }
    return 1;
  }
}

/**
* Process message sent via pipe.
* @pipefd        read from this pipe
* @std_pointer   print job text to this file
* Return -1 on error, 0 on success.
*/
int receive_on_pipe(int pipefd[2], FILE *std_pointer) {
  unsigned char pipe_request;
  if (read(pipefd[0], &pipe_request, sizeof(char)) != sizeof(char)) {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to receive pipe request.\n" RESET, getpid());
    return -1;
  }

  if (pipe_request == (unsigned char) ONE_JOB_REQUEST) {
    int text_length;
    if (read(pipefd[0], &text_length, sizeof(int)) != sizeof(int)) {
      fprintf(stderr, RED ">>> %d <<< [Client Error] Pipe failed to receive data.\n" RESET, getpid());
      return -1;
    }

    char *job_text = (char *) malloc(sizeof(char) * (text_length+1));

    ssize_t received_bytes = 0;
    ssize_t received_currently = 0;
    while (received_bytes < text_length+1) {
      received_currently = read(pipefd[0], job_text + received_bytes, text_length+1 - received_bytes);
      if (received_currently == -1) {
        perror(RED "[Client Error] Pipe failed to receive text" RESET);
        return -1;
      } else {
        received_bytes += received_currently;
      }
    }

    if (debug) {
      size_t msg_size = sizeof(char) + sizeof(int) + received_bytes;
      printf(">>> %d <<< Received message (%li bytes) from client via pipe.\n", getpid(), msg_size);
    }

    if (std_pointer == stdout) {
      if (debug)
        printf(">>> %d <<< Printing job to stdout.\n\n", getpid());
      fprintf(stdout, BLU "%s" RESET "\n", job_text);
      micro_sleep(500); // preserve printing order at the cost of context switch overhead
      free(job_text);
      // add debug printing

    } else if (std_pointer == stderr) {
      if (debug)
        printf(">>> %d <<< Printing job to stderr.\n\n", getpid());
      fprintf(stderr, GRN "%s" RESET "\n", job_text);
      micro_sleep(500); // preserve printing order at the cost of context switch overhead
      free(job_text);
      // add debug printing

    } else {
      fprintf(stderr, RED ">>> %d <<< [Client Error] Failed to print text: unknown file pointer on pipe.\n" RESET, getpid());
      return -1;
    }
    return 0;

  } else if (pipe_request == (unsigned char) STOP_REQUEST) {
    if (debug) {
      printf(">>> %d <<< Received request (%d) from client via pipe.\n", getpid(), STOP_REQUEST);
    }
    return 1;

  } else {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Unknown pipe request encountered (%d).\n" RESET, getpid(), (int) pipe_request);
    return -1;
  }
}


/*====================== MISCELLANIOUS UTILITY METHODS =======================*/

/**
* Validate checksum of received message.
* @msg   attempt to validate checksum of this message
* Return 0 on success, -1 on checksum mismatch.
*/
int validate_checksum(struct JobMessage *msg) {
  if (debug)
    printf(">>> %d <<< Validating checksum.\n", getpid());
  if (msg->text_length == 0)
    return 0;

  unsigned int sum = 0;
  for (unsigned int i = 0; i < strlen(msg->job_text); i++)
    sum += msg->job_text[i];

  unsigned int expected = sum % 32;
  unsigned int received = ((int) msg->job_info) & 31;
  if (expected == received) {
    return 0;
  } else {
    fprintf(stderr, RED ">>> %d <<< [Client Error] Checksum mismatch.\n" RESET, getpid());
    return -1;
  }
}

/**
* Parse a positive integer from string.
* @number_string   number in string form
* Return parsed number on success, -1 on failure.
*/
int parse_number(char *number_string) {
  char *endptr;
  int result = strtol(number_string, &endptr, 10);
  if (endptr == number_string && result == 0)
    result = -1;
  return result;
}

/**
* Suspend process for a time.
* @microseconds   suspend for this many microseconds
* Return 0 on success, -1 on failure.
*/
int micro_sleep(unsigned long microseconds) {
  struct timespec req, rem;
  time_t seconds = (int) (microseconds/1000000);
  time_t nanoseconds = microseconds * 1000;
  req.tv_sec = seconds;
  req.tv_nsec = nanoseconds;
  int sleep_status = nanosleep(&req, &rem);
  if (sleep_status)
    fprintf(stderr, ">>> %d <<< [Client Warning] Failed to sleep for %li ms.\n", getpid(), microseconds);
  return sleep_status;
}

/**
* Signal handler.
* @signum  signal number
*/
void handler(int signum) {
  if (signum == SIGINT) {
    printf(">>> %d <<< Received interrupt signal.\n", getpid());
    interrupted = 1;
  }
}
