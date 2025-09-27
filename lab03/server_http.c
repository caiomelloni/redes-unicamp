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

// Fork cria um novo processo filho
// retorna o pid do filho ao pai
// o processo filho recebe um pid de valor '0'
// 
// em caso de erro ao criar um processo filho, printa o erro
// mas nao para a execucao do server
int Fork() {
  int pid;
  if ((pid = fork()) < 0) {
    perror("fork => erro: não foi possĩvel criar um processo filho");
  }
  return pid;
}

// Accept extrai a primeira conexao na fila de conexoes do listen sockedt
// e devolve um novo file descriptor para essa conexao
//
// em caso de erro, printa o erro
// mas nao para a execucao do server
int Accept(int listenfd) {
  int file_descriptor;
  if ((file_descriptor = accept(listenfd, NULL, NULL)) < 0) {
    perror("accept => erro: não foi possĩvel aceitar uma nova conexão");
  }
  return file_descriptor;
}

// Close fecha a conexao de connfd
//
// retorna 0 em caso de successo
int Close(int connfd) {
  int sucesso;
  if ((sucesso = close(connfd)) < 0) {
    perror("close => erro: não foi possĩvel fechar a conexão");
  }
  return sucesso;
}

// Socket cria um endpoint de comunicacao e retorna um file_descriptor para esse endpoint
//
// em caso de erro, para a execucao do servidor
int Socket() {
  int listenfd;
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket => erro: não foi possível criar um listening socket");
      exit(1);
  }
  return listenfd;
}

// Bind configura o endpoint de conexao criado
//
// para a execucao em caso de erro
// em caso de sucesso, retorna a struct de configuracoes para o listening socket
struct sockaddr_in Bind(int listenfd, int porta) {
  // essa struct guarda qual protocolo estamos usando e qual porta foi alocada pelo socket
  // ela descreve o socket
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
  servaddr.sin_port        = htons(porta);              

  if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
      perror("bind => erro: não foi possível fazer o bind do listening socket do servidor");
      close(listenfd);
      exit(1);
  }

  return servaddr;
}

void process_request(int connfd) {
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

}

// log_server_info printa a porta e ip do servidor 
// tambem divulga as informacoes no arquivo server.info
void log_server_info(int listenfd) {
  struct sockaddr_in bound; socklen_t blen = sizeof(bound);
  if (getsockname(listenfd, (struct sockaddr*)&bound, &blen) == 0) {
      unsigned short p = ntohs(bound.sin_port);
      printf("[SERVIDOR] Escutando em 127.0.0.1:%u\n", p);
      FILE *f = fopen("server.info", "w");
      if (f) { fprintf(f, "IP=127.0.0.1\nPORT=%u\n", p); fclose(f); }
      fflush(stdout);
  }
}


int main(int argc, char **argv) {
    int listenfd, connfd;

    // le a porta via linha de comando
    // se a porta nao for fornecida, entao o sistema operacional ira atribuir uma automaticamente
    int porta = 0;
    if (argc > 1) {
      porta = atoi(argv[1]);
    }
    //
    // cria o socket
    listenfd = Socket(AF_INET, SOCK_STREAM);

    // essa struct guarda qual protocolo estamos usando e qual porta foi alocada pelo socket
    // ela descreve o socket
    struct sockaddr_in servaddr = Bind(listenfd, porta);

    log_server_info(listenfd);

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
          process_request(connfd);
          Close(connfd); // fecha só a conexão aceita; servidor segue escutando
          exit(0);
        }
        // essa conexao ja foi direcionada ao filho
        Close(connfd); // o pai precisa fechar a conexao e continuar recebendo outras
    }

    return 0;
}
