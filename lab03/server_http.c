#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define LISTENQ      0
#define MAXLINE 4096
#define MAXDATASIZE  256

int Fork() {
  int pid;
  if ((pid = fork()) < 0) {
    perror("fork => erro: não foi possĩvel criar um processo filho");
    exit(1);
  }
  return pid;
}

int Accept(int sockfd) {
  int file_descriptor;
  if ((file_descriptor = accept(listenfd, NULL, NULL)) < 0) {
    perror("accept => erro: não foi possĩvel aceitar uma nova conexão");
  }
  return file_descriptor;
}

int Close(int file_descriptor) {
  int sucesso;
  if ((sucesso = close(file_descriptor)) < 0) {
    perror("close => erro: não foi possĩvel fechar a conexão");
  }
  return sucesso;
}


int main(int argc, char **argv) {
    int listenfd, connfd;
    int porta = 0;

    if (argc > 1) {
      porta = atoi(argv[1]);
    }


    // essa struct guarda qual protocolo estamos usando e qual porta foi alocada pelo socket
    // ela descreve o socket
    struct sockaddr_in servaddr;

    // cria o socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return 1;
    }

    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
    servaddr.sin_port        = htons(porta);              

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind");
        close(listenfd);
        return 1;
    }
    // Descobrir porta real e divulgar em arquivo server.info
    struct sockaddr_in bound; socklen_t blen = sizeof(bound);
    if (getsockname(listenfd, (struct sockaddr*)&bound, &blen) == 0) {
        unsigned short p = ntohs(bound.sin_port);
        printf("[SERVIDOR] Escutando em 127.0.0.1:%u\n", p);
        FILE *f = fopen("server.info", "w");
        if (f) { fprintf(f, "IP=127.0.0.1\nPORT=%u\n", p); fclose(f); }
        fflush(stdout);
    }

    // listen
    if (listen(listenfd, LISTENQ) == -1) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    // laço: aceita clientes, envia banner e fecha a conexão do cliente
    for (;;) {
        connfd = Accept(listenfd, NULL, NULL);
        if (connfd == -1) {
            perror("accept");
            continue; // segue escutando
        }
        if ((pid = Fork()) == 0) {
          Close(listenfd);

          time_t ticks = time(NULL); // ctime() já inclui '\n'
          struct sockaddr_in bound; socklen_t blen = sizeof(bound);
          if (getpeername(connfd, (struct sockaddr*)&bound, &blen) == 0) {
              unsigned short p = ntohs(bound.sin_port);
              printf("remoto: %s:%u\n", inet_ntoa(bound.sin_addr), p);
          } else {
              printf("Error: %s\n", strerror(errno));
          }
          // envia banner "Hello + Time"
          char buf[MAXDATASIZE];
          snprintf(buf, sizeof(buf), "Hello from server!\nTime: %.24s\r\n", ctime(&ticks));
          (void)write(connfd, buf, strlen(buf));
          
          // lê a mensagem enviada pelo cliente e imprime na saída padrão
          char banner[MAXLINE + 1];
          ssize_t n = read(connfd, banner, MAXLINE);
          if (n > 0) {
              banner[n] = 0;
              fputs(banner, stdout);
              fflush(stdout);
          }

          Close(connfd); // fecha só a conexão aceita; servidor segue escutando
          exit(0);
        }
    }

    return 0;
}
