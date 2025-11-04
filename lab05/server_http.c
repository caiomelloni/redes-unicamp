/* server_http.c
 *
 * Versão estendida para o Exercício 5:
 *  - Mode 0: servidor concorrente baseado em fork() (original)
 *  - Mode 1: servidor single-process usando select()
 *  - Mode 2: servidor single-process usando poll()
 *  - Mode 3: servidor single-process usando select() para TCP + UDP
 *
 * Compile: gcc -Wall -O2 -o server_http server_http.c
 *
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

/* constantes */
#define LISTENQ      0
#define MAXLINE 4096
#define MAXDATASIZE  256

typedef void Sigfunc(int);   
/* ---------- Prototypes --------------------------------- */
Sigfunc * Signal(int signo, Sigfunc *func);
void sig_chld(int signo);
char* get_time(void);
void echo_servidor(const char* msg);
int Fork(void);
int Accept(int listenfd);
int Close(int connfd);
int Socket(void);
void Setsocketopt(int server_fd);
struct sockaddr_in Bind(int listenfd, int porta);
int Write(char* response, int connfd);
int eh_requisicao_get(const char* request);
void process_request(int connfd, int sleep_time);
void Listen(int listenfd, int tamanho_fila);
void log_server_info(int listenfd);

/* diferentes tipos de rodar um servidor */
void server_with_select(int listenfd, int sleep_time);
void server_with_poll(int listenfd, int sleep_time);
void server_tcp_udp_select(int listenfd, int udpfd, int sleep_time);

/* ------------------------------------------------------- */

/* implementação de Signal (wrapper) */
Sigfunc * Signal(int signo, Sigfunc *func) {
    struct sigaction act, oact;
    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (signo == SIGALRM) {
#ifdef SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT;
#endif
    } else {
#ifdef SA_RESTART
        act.sa_flags |= SA_RESTART;
#endif
    }
    if (sigaction(signo, &act, &oact) < 0)
        return SIG_ERR;
    return oact.sa_handler;
}

/* handler de SIGCHLD para evitar zumbis no modo fork */
void sig_chld(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("[SIGCHLD] child %d terminated\n", pid);
    }
    return;
}

/* get_time: string simples */
char* get_time() {
  time_t ticks = time(NULL);
  return ctime(&ticks);
}

/* echo_servidor com timestamp */
void echo_servidor(const char* msg) {
  printf("[SERVIDOR] (%.24s): %s\n", get_time(), msg);
  fflush(stdout);
}

/* Fork wrapper */
int Fork() {
  int pid;
  if ((pid = fork()) < 0) {
    perror("fork => erro: não foi possĩvel criar um processo filho");
  }
  return pid;
}

/* Accept wrapper (retorna -1 em erro) */
int Accept(int listenfd) {
  struct sockaddr_in cliaddr;
  memset(&cliaddr, 0, sizeof(cliaddr));
  socklen_t cliaddr_len = sizeof(cliaddr);
  int file_descriptor;
  if ((file_descriptor = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len)) < 0) {
    return -1;
  }

  char cli_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &cliaddr.sin_addr, cli_ip, sizeof(cli_ip));
  int cli_port = ntohs(cliaddr.sin_port);

  char cli_info[INET_ADDRSTRLEN + 64];
  snprintf(cli_info, sizeof(cli_info), "nova conexão aceita, cliente: %s:%d (fd=%d)",
           cli_ip, cli_port, file_descriptor);
  echo_servidor(cli_info);

  return file_descriptor;
}

/* Close wrapper */
int Close(int connfd) {
  int sucesso;
  if ((sucesso = close(connfd)) < 0) {
    perror("close => erro: não foi possĩvel fechar a conexão");
  }
  return sucesso;
}

/* Socket para stream (original) */
int Socket() {
  int listenfd;
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket => erro: não foi possível criar um listening socket");
      exit(1);
  }
  return listenfd;
}

/* setsockopt: SO_REUSEADDR */
void Setsocketopt(int server_fd) {
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      perror("setsockopt SO_REUSEADDR failed");
  }
  return;
}

/* Bind: faz bind e retorna struct servaddr (para obter porto se porta 0) */
struct sockaddr_in Bind(int listenfd, int porta) {
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port        = htons(porta);

  if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
    perror("bind => erro: não foi possível fazer o bind do listening socket do servidor");
    close(listenfd);
    exit(1);
  }
  return servaddr;
}

/* Write: escreve resposta no connfd */
int Write(char* response, int connfd) {
  if (response == NULL) return -1;
  return write(connfd, response, strlen(response));
}

/* verifica se request é GET / */
int eh_requisicao_get(const char* request) {
    const char* get_http_1   = "GET / HTTP/1.0";
    const char* get_http_1_1 = "GET / HTTP/1.1";
    return (request && (strstr(request, get_http_1) != NULL || strstr(request, get_http_1_1) != NULL));
}

/* process_request: dorme sleep_time segundos e responde */
void process_request(int connfd, int sleep_time) {
    if (sleep_time > 0) {
        struct timespec ts;
        ts.tv_sec = sleep_time;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }

    char request[MAXLINE + 1];
    ssize_t n = read(connfd, request, MAXLINE);
    if (n > 0) {
        request[n] = '\0';
        echo_servidor("request recebido | msg:");
        fputs(request, stdout);
        fflush(stdout);

        char* response;
        if (eh_requisicao_get(request)) {
            response = "HTTP/1.0 200 OK\r\n"
                       "Content-Type: text/html\r\n"
                       "Content-Length: 91\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "<html><head><title>MC833</title></head><body><h1>MC833</h1></body></html>";
        } else {
            response = "400 Bad Request\n";
        }
        if (Write(response, connfd) == -1) {
            perror("write => erro: não foi possível enviar a mensagem ao cliente");
        }
    } else if (n == 0) {
        /* cliente fechou sem enviar nada */
    } else {
        perror("read");
    }
}

/* Listen wrapper */
void Listen(int listenfd, int tamanho_fila) {
  if (listen(listenfd, tamanho_fila) == -1) {
      perror("listen => erro: não foi possível ativar o listening socket");
      close(listenfd);
      exit(1);
  }
}

/* log_server_info: grava server.info com IP e PORT */
void log_server_info(int listenfd) {
  struct sockaddr_in bound; socklen_t blen = sizeof(bound);
  if (getsockname(listenfd, (struct sockaddr*)&bound, &blen) == 0) {
      unsigned short p = ntohs(bound.sin_port);
      printf("[SERVIDOR] Escutando em 0.0.0.0:%u\n", p);
      FILE *f = fopen("server.info", "w");
      if (f) { fprintf(f, "IP=127.0.0.1\nPORT=%u\n", p); fclose(f); }
      fflush(stdout);
  }
}

/* ------------------ Implementações de multiplexação ------------------ */

/* servidor usando select() — single-process */
void server_with_select(int listenfd, int sleep_time) {
    int maxfd, i;
    int clients[FD_SETSIZE]; /* -1 = free */
    fd_set allset, rset;

    for (i = 0; i < FD_SETSIZE; i++) clients[i] = -1;

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    maxfd = listenfd;

    char buf[128];
    snprintf(buf, sizeof(buf), "[select] pid=%d modo select iniciado (listenfd=%d)", (int)getpid(), listenfd);
    echo_servidor(buf);

    for (;;) {
        rset = allset;
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(listenfd, &rset)) {
            int connfd = Accept(listenfd);
            if (connfd >= 0) {
                for (i = 0; i < FD_SETSIZE; i++) {
                    if (clients[i] < 0) {
                        clients[i] = connfd;
                        break;
                    }
                }
                if (i == FD_SETSIZE) {
                    echo_servidor("[select] too many clients, closing new conn");
                    Close(connfd);
                } else {
                    FD_SET(connfd, &allset);
                    if (connfd > maxfd) maxfd = connfd;
                    snprintf(buf, sizeof(buf), "[select] accepted connfd=%d stored at clients[%d]", connfd, i);
                    echo_servidor(buf);
                }
            }
            if (--nready <= 0) continue;
        }

        for (i = 0; i < FD_SETSIZE; i++) {
            int sockfd = clients[i];
            if (sockfd < 0) continue;
            if (FD_ISSET(sockfd, &rset)) {
                snprintf(buf, sizeof(buf), "[select] pid=%d handling connfd=%d (clients[%d])", (int)getpid(), sockfd, i);
                echo_servidor(buf);
                process_request(sockfd, sleep_time);
                Close(sockfd);
                FD_CLR(sockfd, &allset);
                clients[i] = -1;
                if (--nready <= 0) break;
            }
        }
    }
}

/* servidor usando poll() — single-process */
void server_with_poll(int listenfd, int sleep_time) {
    int i, maxi, connfd, sockfd;
    int nready;
    const int max_clients = 1024; /* razoável para exercício */
    struct pollfd *clients = calloc(max_clients, sizeof(struct pollfd));
    if (!clients) {
        perror("calloc");
        exit(1);
    }

    for (i = 0; i < max_clients; i++) clients[i].fd = -1;
    clients[0].fd = listenfd;
    clients[0].events = POLLRDNORM;
    maxi = 0;

    char buf[128];
    snprintf(buf, sizeof(buf), "[poll] pid=%d modo poll iniciado (listenfd=%d)", (int)getpid(), listenfd);
    echo_servidor(buf);

    for (;;) {
        nready = poll(clients, maxi + 1, -1);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (clients[0].revents & POLLRDNORM) {
            connfd = Accept(listenfd);
            if (connfd >= 0) {
                for (i = 1; i < max_clients; i++) {
                    if (clients[i].fd < 0) {
                        clients[i].fd = connfd;
                        clients[i].events = POLLRDNORM;
                        break;
                    }
                }
                if (i == max_clients) {
                    echo_servidor("[poll] too many clients");
                    Close(connfd);
                } else {
                    if (i > maxi) maxi = i;
                    snprintf(buf, sizeof(buf), "[poll] accepted connfd=%d into client[%d]", connfd, i);
                    echo_servidor(buf);
                }
            }
            if (--nready <= 0) continue;
        }

        for (i = 1; i <= maxi; i++) {
            if (clients[i].fd < 0) continue;
            if (clients[i].revents & (POLLRDNORM | POLLERR)) {
                sockfd = clients[i].fd;
                snprintf(buf, sizeof(buf), "[poll] pid=%d handling connfd=%d (client[%d])", (int)getpid(), sockfd, i);
                echo_servidor(buf);
                process_request(sockfd, sleep_time);
                Close(sockfd);
                clients[i].fd = -1;
                if (--nready <= 0) break;
            }
        }
    }

    free(clients);
}

/* servidor que trata TCP e UDP com select() no mesmo processo */
void server_tcp_udp_select(int listenfd, int udpfd, int sleep_time) {
    int maxfd, i;
    int clients[FD_SETSIZE];
    fd_set allset, rset;

    for (i = 0; i < FD_SETSIZE; i++) clients[i] = -1;

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    FD_SET(udpfd, &allset);
    maxfd = (listenfd > udpfd) ? listenfd : udpfd;

    char buf[256];
    snprintf(buf, sizeof(buf), "[tcp+udp select] pid=%d iniciado (tcp=%d udp=%d)",
             (int)getpid(), listenfd, udpfd);
    echo_servidor(buf);

    for (;;) {
        rset = allset;
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* UDP datagram available? */
        if (FD_ISSET(udpfd, &rset)) {
            char databuf[MAXLINE+1];
            struct sockaddr_in cliaddr; socklen_t clilen = sizeof(cliaddr);
            ssize_t n = recvfrom(udpfd, databuf, MAXLINE, 0, (struct sockaddr*)&cliaddr, &clilen);
            if (n > 0) {
                databuf[n] = '\0';
                snprintf(buf, sizeof(buf), "[UDP] from %s:%d -> %s",
                         inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), databuf);
                echo_servidor(buf);
                if (sleep_time > 0) sleep(sleep_time);
                const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                sendto(udpfd, resp, strlen(resp), 0, (struct sockaddr*)&cliaddr, clilen);
            }
            if (--nready <= 0) continue;
        }

        /* new TCP connection? */
        if (FD_ISSET(listenfd, &rset)) {
            int connfd = Accept(listenfd);
            if (connfd >= 0) {
                for (i = 0; i < FD_SETSIZE; i++) {
                    if (clients[i] < 0) {
                        clients[i] = connfd;
                        break;
                    }
                }
                if (i == FD_SETSIZE) {
                    echo_servidor("[tcp+udp] too many clients");
                    Close(connfd);
                } else {
                    FD_SET(connfd, &allset);
                    if (connfd > maxfd) maxfd = connfd;
                    snprintf(buf, sizeof(buf), "[tcp+udp] accepted connfd=%d stored at clients[%d]", connfd, i);
                    echo_servidor(buf);
                }
            }
            if (--nready <= 0) continue;
        }

        /* existing TCP client data */
        for (i = 0; i < FD_SETSIZE; i++) {
            int sockfd = clients[i];
            if (sockfd < 0) continue;
            if (FD_ISSET(sockfd, &rset)) {
                snprintf(buf, sizeof(buf), "[tcp+udp select] pid=%d handling connfd=%d (clients[%d])",
                         (int)getpid(), sockfd, i);
                echo_servidor(buf);
                process_request(sockfd, sleep_time);
                Close(sockfd);
                FD_CLR(sockfd, &allset);
                clients[i] = -1;
                if (--nready <= 0) break;
            }
        }
    }
}

/* --------------------------- main ----------------------------------- */

int main(int argc, char **argv) {
    int listenfd, connfd;
    int porta = 0;
    if (argc > 1) porta = atoi(argv[1]);

    int backlog = LISTENQ;
    if (argc > 2) backlog = atoi(argv[2]);

    int sleep_time = 0;
    if (argc > 3) sleep_time = atoi(argv[3]);

    int mode = 0;
    if (argc > 4) mode = atoi(argv[4]);

    listenfd = Socket();
    Setsocketopt(listenfd);
    Bind(listenfd, porta);
    log_server_info(listenfd);
    Listen(listenfd, backlog);

    /* handle SIGCHLD only if using fork mode; harmless otherwise */
    Signal(SIGCHLD, sig_chld);

    /* escolha do modo */
    if (mode == 1) {
        server_with_select(listenfd, sleep_time);
        return 0;
    } else if (mode == 2) {
        server_with_poll(listenfd, sleep_time);
        return 0;
    } else if (mode == 3) {
        /* cria UDP socket e bind na mesma porta */
        int udpfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udpfd < 0) { perror("socket udp"); exit(1); }
        /* permitir reutilizar endereco */
        Setsocketopt(udpfd);
        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        /* obter porto real do listenfd (se porta 0 foi pedida) */
        socklen_t len = sizeof(servaddr);
        if (getsockname(listenfd, (struct sockaddr*)&servaddr, &len) < 0) {
            perror("getsockname");
            close(udpfd);
            exit(1);
        }
        /* já temos servaddr.sin_port preenchido com porta do listenfd */
        if (bind(udpfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            perror("bind udp");
            close(udpfd);
            exit(1);
        }
        server_tcp_udp_select(listenfd, udpfd, sleep_time);
        close(udpfd);
        return 0;
    }

    /* modo default: servidor concorrente com fork (original) */
    for (;;) {
        if ((connfd = Accept(listenfd)) < 0) {
            if (errno == EINTR)
                continue;
            else
                perror("accept error");
        }

        pid_t pid;
        if ((pid = Fork()) == 0) {
          /* child */
          Close(listenfd);
          char buf[128];
          snprintf(buf, sizeof(buf), "[fork] pid=%d handling connfd=%d", (int)getpid(), connfd);
          echo_servidor(buf);
          process_request(connfd, sleep_time);
          Close(connfd);
          exit(0);
        }
        /* parent */
        Close(connfd);
    }

    return 0;
}

