#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include "err.h"

#define BUFFER_SIZE 4096
#define PORT_NUM "20160"
#define MSG_MAX_SIZE 1000
#define _STDIN 0
#define _SERVER 1
#define INVALID_NETWORK_MESSAGE 100

struct __attribute__((__packed__)) MessageStructure {
     uint16_t msg_len;
     char message[MSG_MAX_SIZE];
};

int main (int argc, char *argv[]) {
  int rc;
  struct addrinfo addr_hints, *addr_result;
  char* port_num = PORT_NUM;
  char line[BUFFER_SIZE];

  struct MessageStructure msg_to_send;
  struct MessageStructure msg_to_recv;

  int prev_len;
  int remains;
  int rval;
  int msg_len;
  int i;

  struct pollfd poll_fd[2];

  poll_fd[ _STDIN ].fd = 0;
  poll_fd[ _STDIN ].events = POLLIN;
  poll_fd[ _STDIN ].revents = 0;

  poll_fd[ _SERVER ].events = POLLIN;
  poll_fd[ _SERVER ].revents = 0;

  if ((argc <= 1) || (argc > 3)) {
    fatal("Usage: %s host [port]", argv[0]);
  } else if (argc == 3) {
    port_num = argv[2];
  }

  poll_fd[ _SERVER ].fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (poll_fd[ _SERVER ].fd < 0) {
    syserr("socket");
  }

  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_flags = 0;
  addr_hints.ai_family = AF_INET;
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;
  addr_hints.ai_addrlen = 0;
  addr_hints.ai_addr = NULL;
  addr_hints.ai_canonname = NULL;
  addr_hints.ai_next = NULL;
  rc =  getaddrinfo(argv[1], port_num, &addr_hints, &addr_result);
  if (rc != 0) {
    fprintf(stderr, "rc=%d\n", rc);
    syserr("getaddrinfo: %s", gai_strerror(rc));
  }

  if (connect(poll_fd[ _SERVER ].fd, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);

  int ret;
  while (1) {
    for (i = 0; i < 2; i++) {
      poll_fd[i].revents = 0;
    }

    ret = poll(poll_fd, 2, -1);
    if (ret < 0)
      perror("poll");
    else if (ret > 0) {
      if (poll_fd[ _STDIN ].revents  & (POLLIN | POLLERR)) {
        memset(line, 0, sizeof(line));
        read(poll_fd[ _STDIN ].fd, line, sizeof(line) - 1);
        line[strcspn(line, "\n")] = 0;

        if ((strlen(line) > MSG_MAX_SIZE) || (strlen(line) == 0)) {
          // Ignoruje zbyt dluga albo pusta linie
          continue;
        }

        memset(msg_to_send.message, 0, sizeof(msg_to_send.message));
        strncpy(msg_to_send.message, line, sizeof(msg_to_send.message));

        msg_to_send.msg_len = htons(strlen(msg_to_send.message));

        if (write(poll_fd[ _SERVER ].fd, &msg_to_send, sizeof(msg_to_send.msg_len) + strlen(msg_to_send.message)) < 0)
            perror("writing on stream socket");

      } else if (poll_fd[ _SERVER ].revents & (POLLIN | POLLERR)) {

        memset(msg_to_recv.message, 0, sizeof(msg_to_recv.message));
        rval = read(poll_fd[ _SERVER ].fd, ((char*)&msg_to_recv), sizeof(msg_to_recv.msg_len));

        if (rval < 0) {
          perror("Reading stream message");
          if (close(poll_fd[ _SERVER ].fd) < 0)
            perror("close");
        }

        if (rval == 0) {
          fprintf(stderr, "Server ended connection\n");
          if (close(poll_fd[ _SERVER ].fd) < 0)
            perror("close");

          return 0;
        }

        else if (rval == sizeof(msg_to_recv.msg_len)) {
          msg_len = ntohs(msg_to_recv.msg_len);

          if (msg_len > MSG_MAX_SIZE) {
            fprintf(stderr, "Invalid Message Length\n");
            if (close(poll_fd[ _SERVER ].fd) < 0)
              perror("close");

            return INVALID_NETWORK_MESSAGE;
          }

          prev_len = 0;
          do {
            remains = msg_len - prev_len;

            rval = read(poll_fd[ _SERVER ].fd, ((char*)&msg_to_recv.message) + prev_len, remains);
            if (rval < 0) {
              perror("Reading stream message");
              if (close(poll_fd[ _SERVER ].fd) < 0)
                perror("close");
              return 0;
            }

            if (rval > 0) {
              // printf("read %zd bytes from socket\n", rval);
              prev_len += rval;

              if (prev_len == msg_len) { // Finished getting message
                printf("%.*s\n", ntohs(msg_to_recv.msg_len), msg_to_recv.message);
              }
            }
          } while (rval > 0);

          if (prev_len < msg_len) {
            fprintf(stderr, "Server closed connection while sending message\n");
            if (close(poll_fd[ _SERVER ].fd) < 0)
              perror("close");
            return 0;
          }
        }
      }
    }
  }

  return 0;
}
