#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#define BASE_PORT    5000    // base para nodos
#define CLIENT_PORT  7000    // porta para receber ID do lider
#define INTERVAL     6      // segundos entre envios
#define MONITOR_PORT 6000

enum msg_type { CLIENT_PROPOSE = 1000, CLIENT_OK };

typedef struct msg {
    int type;
    int value;
} msg;

int receive_leader() {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
    struct sockaddr_in sin = { .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(CLIENT_PORT) };
    if (bind(srv, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind"); exit(1);
    }
    listen(srv, 1);
    printf("[Client] Aguardando líder na porta %d...\n", CLIENT_PORT);
    int c = accept(srv, NULL, NULL);
    if (c < 0) { perror("accept"); exit(1); }
    int leader_id;
    if (read(c, &leader_id, sizeof(leader_id)) != sizeof(leader_id)) {
        fprintf(stderr, "[Client] Erro lendo líder\n"); exit(1);
    }
    close(c);
    close(srv);
    printf("[Client] Líder eleito: %d\n", leader_id);
    return leader_id;
}

int receive_new_leader() {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //para evitar conflito com a porta
    struct sockaddr_in sin = { .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(CLIENT_PORT) };
    if (bind(srv, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind"); exit(1);
    }
    listen(srv, 1);
    printf("[Client] Aguardando novo lider na porta %d...\n", CLIENT_PORT);
    int c = accept(srv, NULL, NULL);
    if (c < 0) { perror("accept"); exit(1); }
    int leader_id;
    if (read(c, &leader_id, sizeof(leader_id)) != sizeof(leader_id)) {
        fprintf(stderr, "[Client] Erro lendo novo líder\n"); exit(1);
    }
    close(c);
    close(srv);
    printf("[Client] Novo lider eleito: %d\n", leader_id);
    return leader_id;
}

void send_monitor(const char *line) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(MONITOR_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    sendto(sock, line, strlen(line), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

void timestamp(char *buf, size_t sz) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%S", tm);
    int ms = tv.tv_usec/1000;
    snprintf(buf + strlen(buf), sz - strlen(buf), ".%03d", ms);
}

int main() {
    
    int leader_id = receive_leader();
   
    char ts[32], buf[128];
    timestamp(ts, sizeof(ts));
    snprintf(buf, sizeof(buf), "%s,client,%d,RECV_LEADER,,\n", ts, leader_id);
    send_monitor(buf);


    int valores[] = {42, 99, 7, 1234, 56};
    int n = sizeof(valores) / sizeof(valores[0]);

    for (int i = 0; i < n; i++) {
        int val = valores[i];
        int port = BASE_PORT + 100 + leader_id;
        sleep(2);

        int enviado = 0;
        while (!enviado) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr = { .sin_family = AF_INET,
                .sin_port = htons(port),
                .sin_addr.s_addr = inet_addr("127.0.0.1") };
            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                msg m = { CLIENT_PROPOSE, val };
                write(sock, &m, sizeof(m));
                printf("[Client] valor enviado %d ao lider %d\n", val, leader_id);

                char ts[32], buf[128];
                timestamp(ts, sizeof(ts));
                snprintf(buf, sizeof(buf), "%s,client,%d,SEND_VALUE,,%d\n", ts, leader_id, val);
                send_monitor(buf);
            } else {
                perror("[Client] connect");
            }
            close(sock);

            int ack_srv = socket(AF_INET, SOCK_STREAM, 0);
            int opt = 1;
            setsockopt(ack_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
            struct sockaddr_in csin = { .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = htons(CLIENT_PORT+1) };
            if (bind(ack_srv, (struct sockaddr *)&csin, sizeof(csin)) < 0) {
                perror("bind ack"); exit(1);
            }
            listen(ack_srv, 1);

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(ack_srv, &fds);
            struct timeval tv = {8, 0};
            int sel = select(ack_srv+1, &fds, NULL, NULL, &tv);
            if (sel > 0) {
                int c2 = accept(ack_srv, NULL, NULL);
                if (c2 >= 0) {
                    msg r;
                    if (read(c2, &r, sizeof(r)) == sizeof(r) && r.type == CLIENT_OK) {
                        printf("[Client] Recebido OK para %d\n", r.value);
                        char ts[32], buf[128];
                        timestamp(ts, sizeof(ts));
                        snprintf(buf, sizeof(buf), "%s,client,%d,RECV_OK,,%d\n", ts, leader_id, r.value);
                        send_monitor(buf);
                        enviado = 1;
                    } else {
                        fprintf(stderr, "[Client] ACK invalido\n");
                    }
                    close(c2);
                }
            } else {
                sleep(10);
                printf("[Client] Timeout esperando ACK do lider %d para valor %d. Aguardando novo lider...\n", leader_id, val);
                leader_id = receive_new_leader();
                port = BASE_PORT + 100 + leader_id;
            }
            close(ack_srv);
        }
        if (i < n-1) sleep(INTERVAL);
    }
    printf("[Client] Todas propostas enviadas. Saindo...\n");
    return 0;
}
