#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#define BASE_PORT       5000
#define MONITOR_PORT    6000
#define CLIENT_PORT     7000    // porta onde client escuta
#define CLIENT_ACK_PORT (CLIENT_PORT + 1)
#define NODES           5
#define QUEUE_CAPACITY  128
#define ELECTION_TIMEOUT 3      // segundos para coletar candidaturas
#define TIMEOUT_SEC     5       // intervalo Paxos
#define KNOWN_STATES    5

enum msg_type { ELECTION, COORDINATOR, PREPARE, PROMISE, ACCEPT, ACCEPTED, HEARTBEAT };

typedef struct msg {
    enum msg_type type;
    int from_id;
    int proposal_num;
    int proposal_val;   
} msg;


typedef struct msg_queue {
    msg data[QUEUE_CAPACITY];
    int head, tail, size;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
} msg_queue;

static msg_queue inbox;
static int election_done = 0;
static int leader_id = -1;
static int highest_proposal = 0;
static int promise_count = 0;
static int accepted_count = 0;
static int accepted_value = -1;
static int proposal_value = -1; 

static int known_states[KNOWN_STATES] = {42, 99, 7, 1234, 56}; 

static pthread_mutex_t proposal_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t proposal_cond = PTHREAD_COND_INITIALIZER;
static int new_proposal = 0; // flag para nova proposta
static int fail_case = 0; // 0 = normal, 2 = lider cai após 1a proposta, 3 = nó não-lider cai
static int leader_alive = 1;
static time_t last_heartbeat = 0;

void queue_init(msg_queue *q) {
    q->head = q->tail = q->size = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void enqueue(msg_queue *q, msg *m) {
    pthread_mutex_lock(&q->mtx);
    if (q->size < QUEUE_CAPACITY) {
        q->data[q->tail++] = *m;
        if (q->tail >= QUEUE_CAPACITY) q->tail = 0;
        q->size++;
        pthread_cond_signal(&q->cond);
    }
    pthread_mutex_unlock(&q->mtx);
}

int dequeue(msg_queue *q, msg *m) {
    pthread_mutex_lock(&q->mtx);
    while (q->size == 0) pthread_cond_wait(&q->cond, &q->mtx);
    *m = q->data[q->head++];
    if (q->head >= QUEUE_CAPACITY) q->head = 0;
    q->size--;
    pthread_mutex_unlock(&q->mtx);
    return 1;
}

void send_msg(int target_id, msg *m) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(BASE_PORT + target_id),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        write(sock, m, sizeof(*m));
    }
    close(sock);
}

void send_monitor(const char *line) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(MONITOR_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    sendto(sock, line, strlen(line), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

void inform_client(int elected_id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(CLIENT_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    int tentativas = 0;
    while (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0 && tentativas < 10) {
        usleep(200000); // espera 200ms
        tentativas++;
    }
    if (tentativas < 10) {
        write(sock, &elected_id, sizeof(elected_id));
    }
    close(sock);
}

void send_client_ok(int value) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(CLIENT_ACK_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    // tenta conectar algumas vezes
    int tentativas = 0;
    while (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0 && tentativas < 10) {
        usleep(200000);
        tentativas++;
    }
    if (tentativas < 10) {
        struct { int type; int value; } msg = { 1001, value }; // CLIENT_OK
        write(sock, &msg, sizeof(msg));
    }
    close(sock);
}

void *client_listener(void *arg) {
    int node_id = (int)(intptr_t)arg;
    int port = BASE_PORT + 100 + node_id;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port) };
    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Node] Erro no bind do client_listener");
        exit(1);
    }
    if (listen(server, 5) < 0) {
        perror("[Node] Erro no listen do client_listener");
        exit(1);
    }
    printf("[Node %d] client_listener escutando na porta %d\n", node_id, port);
    int propostas_recebidas = 0;
    while (1) {
        int c = accept(server, NULL, NULL);
        struct { int type; int value; } m;
        if (read(c, &m, sizeof(m)) == sizeof(m) && m.type == 1000) { 
            pthread_mutex_lock(&proposal_mtx);
            proposal_value = m.value; 
            new_proposal = 1;
            pthread_cond_signal(&proposal_cond);
            pthread_mutex_unlock(&proposal_mtx);
            printf("[Node %d] recebido valor %d do cliente\n", node_id, m.value);
            propostas_recebidas++;
            if (fail_case == 2 && node_id == leader_id && propostas_recebidas == 1) {
                printf("[Node %d] s falha do lider após 1a proposta\n", node_id);
                exit(99);
            }
            if (fail_case == 4 && node_id == leader_id && propostas_recebidas == 2) {
                printf("[Node %d] simulando falha do lider após 2a proposta\n", node_id);
                exit(96);
            }
        }
        close(c);
    }
    return NULL;
}

void timestamp(char *buf, size_t sz) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%S", tm);
    int ms = tv.tv_usec/1000;
    snprintf(buf + strlen(buf), sz - strlen(buf), ".%03d", ms);
}

void *listener(void *arg) {
    int node_id = (int)(intptr_t)arg;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin = { .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(BASE_PORT + node_id) };
    bind(server, (struct sockaddr*)&sin, sizeof(sin));
    listen(server, NODES);
    while (1) {
        int c = accept(server, NULL, NULL);
        msg m;
        if (read(c, &m, sizeof(m)) == sizeof(m)) {
            if (m.type == HEARTBEAT) {
                last_heartbeat = time(NULL);
            } else {
                enqueue(&inbox, &m);
            }
        }
        close(c);
    }
    return NULL;
}

void *election(void *arg) {
    int node_id = (int)(intptr_t)arg; 
    srand(time(NULL) + node_id);
    int my_num = rand() % 10000;
    msg m = { ELECTION, node_id, 0, my_num };
    for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i, &m);

    int best_num = my_num, best_id = node_id, received = 0;
    while (received < NODES - 1) {
        msg r;
        dequeue(&inbox, &r);
        if (r.type == ELECTION) {
            if (r.proposal_val > best_num || (r.proposal_val == best_num && r.from_id > best_id)) {
                best_num = r.proposal_val;
                best_id = r.from_id;
            }
            received++;
        }
    }
    leader_id = best_id;
    msg coord = { COORDINATOR, node_id, 0, best_id };
    for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i, &coord);
    char buf[128], ts[32];
    timestamp(ts, sizeof(ts));
    snprintf(buf, sizeof(buf), "%s,%d,all,ELECT,%d,%d\n", ts, node_id, my_num, best_id);
    send_monitor(buf);
    election_done = 1;
    printf("[Node %d] Leader elected: %d\n", node_id, leader_id);
    if (node_id == leader_id) {
       
       
        sleep(1);
        inform_client(leader_id);
        pthread_t ct;
        pthread_create(&ct, NULL, client_listener, (void*)(intptr_t)node_id); 
        pthread_detach(ct);
    }
    return NULL;
}

void *paxos(void *arg) {
    int node_id = (int)(intptr_t)arg;
    while (!election_done) usleep(100000);

    if (fail_case == 3 && node_id != leader_id) {
        printf("[Node %d] simulando falha de no\n", node_id);
        exit(97);
    }

    while (1) {
        if (node_id == leader_id) {
            pthread_mutex_lock(&proposal_mtx);
            while (!new_proposal) pthread_cond_wait(&proposal_cond, &proposal_mtx);
            int val = proposal_value;
            new_proposal = 0;
            pthread_mutex_unlock(&proposal_mtx);

            char ts2[32], buf2[128];
            timestamp(ts2, sizeof(ts2));
            snprintf(buf2, sizeof(buf2), "%s,%d,client,RECV_VALUE,%d,\n", ts2, node_id, val);
            send_monitor(buf2);

            int valido = 0;
            for (int i = 0; i < KNOWN_STATES; i++) {
                if (known_states[i] == val) {
                    valido = 1;
                    break;
                }
            }
            if (!valido) {
                printf("[Node %d] Valor invalido recebido do client: %d\n", node_id, val);
                continue;
            }

            highest_proposal++;
            promise_count = 0;
            accepted_count = 0;
            msg prep = { PREPARE, node_id, highest_proposal, 0 };
            char buf[128], ts[32]; timestamp(ts,sizeof(ts));
            snprintf(buf,sizeof(buf), "%s,%d,all,SEND,%d,\n", ts, node_id, highest_proposal);
            send_monitor(buf);
            for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i,&prep);

            int promises = 1;
            while (promises <= NODES/2) {
                msg r;
                dequeue(&inbox, &r);
                if (r.type == PROMISE && r.proposal_num == highest_proposal) {
                    promises++;
                } else if (r.type == COORDINATOR) {
                    leader_id = r.proposal_val;
                }
            }

            msg acc = { ACCEPT, node_id, highest_proposal, val };
            for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i,&acc);

            int accepteds = 1;
            accepted_value = val;
            while (accepteds <= NODES/2) {
                msg r;
                dequeue(&inbox, &r);
                if (r.type == ACCEPTED && r.proposal_num == highest_proposal) {
                    accepteds++;
                } else if (r.type == COORDINATOR) {
                    leader_id = r.proposal_val;
                }
            }

            printf("[Node %d] CONSENSUS on %d\n", node_id, val);
            send_client_ok(val);
        } else {
            msg r;
            dequeue(&inbox, &r);
            if (r.type == COORDINATOR) {
                leader_id = r.proposal_val;
            } else if (r.type == PREPARE) {
                msg prom = { PROMISE, node_id, r.proposal_num, accepted_value };
                send_msg(r.from_id, &prom);
            } else if (r.type == ACCEPT) {
                int aceito = 0;
                for (int i = 0; i < KNOWN_STATES; i++) {
                    if (known_states[i] == r.proposal_val) {
                        aceito = 1;
                        break;
                    }
                }
                if (aceito) {
                    accepted_value = r.proposal_val;
                    printf("[Node %d] Aceitou valor %d do lider %d (proposal_num=%d)\n", node_id, r.proposal_val, r.from_id, r.proposal_num);
                    char ts[32], buf[128];
                    timestamp(ts, sizeof(ts));
                    snprintf(buf, sizeof(buf), "%s,%d,%d,RECV_ACCEPT,%d,%d\n", ts, node_id, r.from_id, r.proposal_num, r.proposal_val);
                    send_monitor(buf);

                    msg accd = { ACCEPTED, node_id, r.proposal_num, accepted_value };
                    send_msg(r.from_id, &accd);

                    printf("[Node %d] Enviou ACCEPTED para o lider %d (proposal_num=%d, valor=%d)\n", node_id, r.from_id, r.proposal_num, accepted_value);
                    timestamp(ts, sizeof(ts));
                    snprintf(buf, sizeof(buf), "%s,%d,%d,SEND_ACCEPTED,%d,%d\n", ts, node_id, r.from_id, r.proposal_num, accepted_value);
                    send_monitor(buf);
                } else {
                    printf("[Node %d] rejeitou valor %d do lider %d (proposal_num=%d)\n", node_id, r.proposal_val, r.from_id, r.proposal_num);
                }
            }
        }
        usleep(10000);
    }
    return NULL;
}

void *heartbeat_sender(void *arg) {
    int node_id = (int)(intptr_t)arg;
    while (1) {
        if (node_id == leader_id && election_done) {
            msg hb = { HEARTBEAT, node_id, 0, 0 };
            for (int i = 1; i <= NODES; i++) {
                if (i != node_id) send_msg(i, &hb);
            }
        }
        sleep(1);
    }
    return NULL;
}

void *leader_monitor(void *arg) {
    int node_id = (int)(intptr_t)arg;
    while (1) {
        if (election_done && node_id != leader_id) {
            time_t now = time(NULL);
            if (last_heartbeat != 0 && now - last_heartbeat > 3) {
                printf("[Node %d] detectado lider %d falhou! chamando reeleicao...\n", node_id, leader_id);
                election_done = 0;
                leader_id = -1;
                last_heartbeat = 0;
                pthread_t et;
                pthread_create(&et, NULL, election, (void*)(intptr_t)node_id);
                pthread_detach(et);
            }
        }
        sleep(1);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int node_id = 5; 
    if (argc >= 2) fail_case = atoi(argv[1]);
    char *env = getenv("PAXOS_FAIL_CASE");
    if (env) fail_case = atoi(env);

    queue_init(&inbox);
    pthread_t lt, et, pt, hb, lm;
    pthread_create(&lt, NULL, listener, (void*)(intptr_t)node_id);
    pthread_create(&et, NULL, election, (void*)(intptr_t)node_id);
    pthread_create(&pt, NULL, paxos, (void*)(intptr_t)node_id);
    pthread_create(&hb, NULL, heartbeat_sender, (void*)(intptr_t)node_id);
    pthread_create(&lm, NULL, leader_monitor, (void*)(intptr_t)node_id);
    pthread_join(lt, NULL);
    pthread_join(et, NULL);
    pthread_join(pt, NULL);
    return 0;
}