#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define TRUE 1
#define FALSE 0
#define MSG_MAX_SIZE 1000
#define PORT_NUM 20160
#define MAX_CLIENTS 21

struct __attribute__((__packed__)) MessageStructure {
    uint16_t msg_len;
    char message[MSG_MAX_SIZE];
};

struct ClientInfoStructure {
  int prev_len;
  int remains;
  uint16_t msg_len;
  struct MessageStructure message;
};

static int finish = FALSE;

/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
  finish = TRUE;
  fprintf(stderr,
          "Signal %d catched. No new connections will be accepted.\n", sig);
}

int main (int argc, char *argv[]) {
  struct pollfd client[MAX_CLIENTS];
  struct ClientInfoStructure client_info[MAX_CLIENTS];

  struct sockaddr_in server;
  size_t length;
  ssize_t rval;
  int msgsock, activeClients, i, j, ret;

  if (signal(SIGINT, catch_int) == SIG_ERR) {
    perror("Unable to change signal handler");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < MAX_CLIENTS; ++i) {
    client[i].fd = -1;
    client[i].events = POLLIN;
    client[i].revents = 0;
  }
  activeClients = 0;

  client[0].fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client[0].fd < 0) {
    perror("Opening stream socket");
    exit(EXIT_FAILURE);
  }

  int port_num = PORT_NUM;

  if (argc >= 3) {
    fprintf(stderr,
            "Usage: %s [port]\n", argv[0]);
    return 0;
  }
  if (argc == 2) {
    port_num = atoi(argv[1]);
  }

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port_num);
  if (bind(client[0].fd, (struct sockaddr*)&server,
           (socklen_t)sizeof(server)) < 0) {
    perror("Binding stream socket");
    exit(EXIT_FAILURE);
  }

  length = sizeof(server);
  if (getsockname (client[0].fd, (struct sockaddr*)&server,
                   (socklen_t*)&length) < 0) {
    perror("Getting socket name");
    exit(EXIT_FAILURE);
  }

  if (listen(client[0].fd, 5) == -1) {
    perror("Starting to listen");
    exit(EXIT_FAILURE);
  }

  do {
    for (i = 0; i < MAX_CLIENTS; ++i)
      client[i].revents = 0;

    if (finish == TRUE && client[0].fd >= 0) {
      if (close(client[0].fd) < 0)
        perror("close");
      client[0].fd = -1;
    }

    ret = poll(client, MAX_CLIENTS, -1);
    if (ret < 0)
      perror("poll");
    else if (ret > 0) {
      if (finish == FALSE && (client[0].revents & POLLIN)) {
        msgsock =
          accept(client[0].fd, (struct sockaddr*)0, (socklen_t*)0);
        if (msgsock == -1)
          perror("accept");
        else {
          for (i = 1; i < MAX_CLIENTS; ++i) {
            if (client[i].fd == -1) {
              client[i].fd = msgsock;

              client_info[i].prev_len = 0;
              client_info[i].remains = 0;
              client_info[i].msg_len = 0;
              memset(&client_info[i].message, 0, sizeof(client_info[i].message));

              activeClients += 1;
              break;
            }
          }
          if (i >= MAX_CLIENTS) {
            fprintf(stderr, "Too many clients\n");
            if (close(msgsock) < 0)
              perror("close");
          }
        }
      }
      for (i = 1; i < MAX_CLIENTS; ++i) {
        if (client[i].fd != -1
            && (client[i].revents & (POLLIN | POLLERR))) {

          if (client_info[i].msg_len == 0) {  // Get Message Length
            rval = read(client[i].fd, ((char*)&client_info[i].message.msg_len), sizeof(client_info[i].message.msg_len));

            if (rval <= 0) {
              if (rval == 0)
                fprintf(stderr, "Ending connection\n");
              else
                perror("Reading stream message");

              if (close(client[i].fd) < 0)
                perror("close");
              client[i].fd = -1;
              activeClients -= 1;
            }

            else if (rval == sizeof(client_info[i].message.msg_len)) {
              client_info[i].msg_len = ntohs(client_info[i].message.msg_len);

              if ((client_info[i].msg_len > MSG_MAX_SIZE) || (client_info[i].msg_len == 0)) {
                fprintf(stderr, "Invalid Message Length\n");
                if (close(client[i].fd) < 0)
                  perror("close");
                client[i].fd = -1;
                activeClients -= 1;
              }
            }
          }

          else { // Get Message (might be partially)
            client_info[i].remains = client_info[i].msg_len - client_info[i].prev_len;

            rval = read(client[i].fd, ((char*)&client_info[i].message.message) + client_info[i].prev_len, client_info[i].remains);

            if ((rval == 0) && (client_info[i].prev_len < client_info[i].msg_len)) {
              fprintf(stderr, "Client closed connection while sending message\n");
              if (close(client[i].fd) < 0)
                perror("close");
              client[i].fd = -1;
              activeClients -= 1;
            }
            if (rval < 0) {
              perror("Reading stream message");
              if (close(client[i].fd) < 0)
                perror("close");
              client[i].fd = -1;
              activeClients -= 1;
            }

            if (rval > 0) {
              printf("read %zd bytes from socket\n", rval);
              client_info[i].prev_len += rval;
              if (client_info[i].prev_len == client_info[i].msg_len) { // Finished getting message

                for (j = 1; j < MAX_CLIENTS; ++j) {
                  if ((client[j].fd != -1) && (i != j)) {
                    if (write(client[j].fd, &client_info[i].message, sizeof(client_info[i].message.msg_len) + client_info[i].msg_len))
                      perror("writing on stream socket");
                  }
                }

                // Reset client's message
                client_info[i].prev_len = 0;
                client_info[i].remains = 0;
                client_info[i].msg_len = 0;
                memset(&client_info[i].message, 0, sizeof(client_info[i].message));
              }
            }
          }
        }
      }
    }
  } while (finish == FALSE || activeClients > 0);

  if (client[0].fd >= 0)
    if (close(client[0].fd) < 0)
      perror("Closing main socket");
  exit(EXIT_SUCCESS);
}
