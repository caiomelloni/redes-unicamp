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
#include <sys/wait.h>
#include <signal.h>



#define LISTENQ      0
#define MAXLINE 4096
#define MAXDATASIZE  256

typedef void Sigfunc(int);   /* handler takes int, returns void */

/* 
 * Signal instala uma nova função de tratamento (handler) para um sinal
 * específico e retorna o handler anterior.
 */

Sigfunc * Signal(int signo, Sigfunc *func) {
    struct sigaction act, oact; 
    /* 
     * act  → estrutura usada para definir o novo comportamento do sinal.
     * oact → estrutura usada para armazenar o comportamento antigo,
     *         que será retornado ao final da função.
     */

    act.sa_handler = func;   
    /*
     * Define a função de tratamento (handler) que será chamada
     * quando o sinal 'signo' for recebido.
     */

    sigemptyset(&act.sa_mask);
    /*
     * Inicializa o conjunto de sinais a serem bloqueados enquanto o
     * handler estiver sendo executado.
     * Aqui estamos deixando o conjunto vazio, ou seja, nenhum outro
     * sinal será bloqueado além do próprio 'signo' — que o kernel
     * já bloqueia automaticamente durante a execução do handler.
     */

    act.sa_flags = 0;
    /*
     * Inicializa as flags de comportamento. Elas controlam detalhes
     * de como o kernel entrega sinais e como as chamadas de sistema
     * (syscalls) se comportam quando um sinal é recebido.
     */

    if (signo == SIGALRM) {
        /* 
         * Sinais de alarme (SIGALRM) costumam ser usados como 
         * "timeout" para chamadas bloqueantes. 
         * 
         * Em sistemas antigos (SunOS 4.x), definimos SA_INTERRUPT 
         * para que as syscalls sejam *interrompidas* (não reiniciadas)
         * quando o sinal chega. Isso permite detectar o timeout.
         */
#ifdef SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT; /* SunOS 4.x */
#endif
    } else {
        /* 
         * Para outros sinais, preferimos que as syscalls sejam
         * automaticamente *reiniciadas* após o retorno do handler,
         * evitando erros EINTR (Interrupted system call).
         * 
         * Isso é o comportamento padrão em sistemas modernos
         * (SVR4, BSD, Linux).
         */
#ifdef SA_RESTART
        act.sa_flags |= SA_RESTART; /* SVR4, 4.4BSD */
#endif
    }

    /*
     * Agora instalamos o novo handler usando sigaction().
     *
     * - signo: sinal que queremos capturar (ex: SIGINT, SIGCHLD, etc.)
     * - &act : nova configuração
     * - &oact: armazena a configuração anterior (para retornar ao chamador)
     */
    if (sigaction(signo, &act, &oact) < 0)
        return (SIG_ERR); 
        /*
         * Caso sigaction retorne erro (< 0),
         * devolvemos SIG_ERR, conforme a convenção da API signal().
         */

    return (oact.sa_handler);
    /*
     * Retorna o antigo handler, permitindo restaurá-lo depois,
     * se necessário. Se não havia handler anterior, pode ser
     * SIG_DFL (ação padrão) ou SIG_IGN (ignorar o sinal).
     */
}

void sig_chld(int signo)
{
    pid_t pid;
    int stat;

    // Recolhe todos os processos filhos que terminaram
    // (-1 = qualquer filho, WNOHANG = não bloqueia)
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("child %d terminated\n", pid);
    }

    // Sai do handler (opcional)
    return;
}

// get_time returna uma string com a data atual na forma "Wed Jun 30 21:49:08 1993\n" 
char* get_time() {
  time_t ticks = time(NULL); // ctime() já inclui '\n'
  return ctime(&ticks);
}

void echo_servidor(char* msg) {
  printf("[SERVIDOR] (%.24s): %s\r\n", get_time(), msg);
}

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

  char cli_info[INET_ADDRSTRLEN + 54];
  snprintf(cli_info, sizeof(cli_info), "nova conexão aceita, cliente: %s:%d", cli_ip, cli_port);
  echo_servidor(cli_info);
  
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
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
  servaddr.sin_port        = htons(porta);              

  if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
    perror("bind => erro: não foi possível fazer o bind do listening socket do servidor");
    close(listenfd);
    exit(1);
  }

  return servaddr;
}

int Write(char* response, int connfd) {
  char buf[MAXDATASIZE];
  snprintf(buf, sizeof(buf), "%s", response);
  return write(connfd, response, strlen(response));
}
 
int eh_requisicao_get(const char* request) {
    char* get_http_1   = "GET / HTTP/1.0";
    char* get_http_1_1 = "GET / HTTP/1.1";

    return strstr(request, get_http_1) != NULL || strstr(request, get_http_1_1) != NULL;
}


void process_request(int connfd, int sleep_time) {
    struct timespec ts;
    ts.tv_sec = sleep_time;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
  // lê a mensagem enviada pelo cliente e imprime na saída padrão
  char request[MAXLINE + 1];
  ssize_t n = read(connfd, request, MAXLINE);
  if (n > 0) {
      request[n] = 0; // coloca '\0' no final da string, garante que seja uma string C valida
      char* response;

      echo_servidor("request recebido | msg:");
      fputs(request, stdout);
      fflush(stdout);

      int ok;
      if (eh_requisicao_get(request)) {
        response = "HTTP/1.0 200 OK\r\n"
                   "Content-Type: text/html\r\n"
                   "Content-Length: 91\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "<html><head><title>MC833</title</head><body><h1>MC833 TCP"
                   "Concorrente </h1></body></html>";
      } else {
        response = "400 Bad Request\n";
      }  
      ok = Write(response, connfd) != -1;
      if (!ok) {
        perror("write => erro: não foi possível enviar a mensagem ao cliente");
      }
  }

}

// Listen muda o socket para o estado de listening e seta o tamanho maximo da fila de conexões
//
// em caso de erro, para a execução do serve
void Listen(int listenfd, int tamanho_fila) {
  // listen
  if (listen(listenfd, tamanho_fila) == -1) {
      perror("listen => erro: não foi possível ativar o listning socket");
      close(listenfd);
      exit(1);
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

    // le a porta via linha de comando no primeiro argumento
    // se a porta nao for fornecida, entao o sistema operacional ira atribuir uma automaticamente
    int porta = 0;
    if (argc > 1) {
      porta = atoi(argv[1]);
    }
    // le o numero de backlog pela linha de comando, no segundo argumento; sera 0 caso nao seja especificado
    int backlog = LISTENQ;
    if (argc > 2) {
      backlog = atoi(argv[2]);
    }
    // le o tempo de delay para cada processo no terceiro argumento; sera 0 caso nao seja especificado
    int sleep_time = 0;
    if (argc> 3) {
      sleep_time = atoi(argv[3]);
    }

    // cria o socket
    listenfd = Socket();

    Bind(listenfd, porta);

    log_server_info(listenfd);

    Listen(listenfd, backlog);
    Signal(SIGCHLD, sig_chld);


    // laço: aceita clientes, processa requisicao e fecha a conexão do cliente
    for (;;) {
        if((connfd = Accept(listenfd)) < 0) {
          if (errno == EINTR)
            continue; /* se for tratar o sinal,quando voltar dá erro em funções lentas */
          else
            perror("accept error");
        }

        pid_t pid;
        if ((pid = Fork()) == 0) {
          Close(listenfd);
          process_request(connfd, sleep_time);
          Close(connfd); // fecha só a conexão aceita; servidor segue escutando
          exit(0);
        }
        // essa conexao ja foi direcionada ao filho
        Close(connfd); // o pai precisa fechar a conexao e continuar recebendo outras
    }

    return 0;
}
