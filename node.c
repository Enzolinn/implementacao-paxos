// node.c
// Paxos node with simple highest-random-number leader election and fixed proposal value

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

#define BASE_PORT       5000
#define MONITOR_PORT    6000
#define NODES           5
#define QUEUE_CAPACITY  128
#define ELECTION_TIMEOUT 3   // seconds to wait for election values
#define TIMEOUT_SEC     5    // Paxos proposal interval
#define PROPOSAL_VALUE  42   // fixed proposal value

// Message types
enum msg_type { ELECTION, COORDINATOR, PREPARE, PROMISE, ACCEPT, ACCEPTED };

typedef struct msg {
    enum msg_type type;
    int from_id;
    int proposal_num;
    int proposal_val;   // for ELECTION: candidate number; for ACCEPT: proposal value
} msg;

// Simple queue
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

// TCP send
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

// UDP to monitor
void send_monitor(const char *line) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(MONITOR_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1") };
    sendto(sock, line, strlen(line), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

// Timestamp helper
void timestamp(char *buf, size_t sz) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%S", tm);
    int ms = tv.tv_usec/1000;
    snprintf(buf + strlen(buf), sz - strlen(buf), ".%03d", ms);
}

// Listener thread: enfileira todas as mensagens
void *listener(void *arg) {
    int node_id = *(int*)arg;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin = { .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(BASE_PORT + node_id) };
    bind(server, (struct sockaddr*)&sin, sizeof(sin));
    listen(server, NODES);
    while (1) {
        int c = accept(server, NULL, NULL);
        msg m;
        if (read(c, &m, sizeof(m)) == sizeof(m)) enqueue(&inbox, &m);
        close(c);
    }
    return NULL;
}

// Election thread: simple highest-random-number
void *election(void *arg) {
    int node_id = *(int*)arg;
    // seed and generate
    srand(time(NULL) + node_id);
    int my_num = rand() % 10000;
    // broadcast ELECTION
    msg m = { ELECTION, node_id, 0, my_num };
    for (int i = 1; i <= NODES; i++) {
        if (i == node_id) continue;
        send_msg(i, &m);
    }
    // record own
    int best_num = my_num;
    int best_id = node_id;
    // collect NODES-1 values
    int received = 0;
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
    // broadcast COORDINATOR
    msg coord = { COORDINATOR, node_id, 0, best_id };
    for (int i = 1; i <= NODES; i++) {
        if (i == node_id) continue;
        send_msg(i, &coord);
    }
    // log monitor
    char buf[128], ts[32];
    timestamp(ts, sizeof(ts));
    snprintf(buf, sizeof(buf), "%s,%d,all,ELECT,%d,%d\n", ts, node_id, my_num, best_id);
    send_monitor(buf);
    election_done = 1;
    printf("[Node %d] Leader elected: %d\n", node_id, leader_id);
    return NULL;
}

// Paxos thread: only after election_done
void *paxos(void *arg) {
    int node_id = *(int*)arg;
    while (!election_done) usleep(100000);
    int last = 0;
    while (1) {
        time_t now = time(NULL);
        if (node_id == leader_id && now - last >= TIMEOUT_SEC) {
            highest_proposal++;
            msg prep = { PREPARE, node_id, highest_proposal, 0 };
            char buf[128], ts[32]; timestamp(ts,sizeof(ts));
            snprintf(buf,sizeof(buf), "%s,%d,all,SEND,%d,\n", ts, node_id, highest_proposal);
            send_monitor(buf);
            for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i, &prep);
            last = now;
        }
        msg r;
        if (dequeue(&inbox, &r)) {
            if (r.type == COORDINATOR) {
                leader_id = r.proposal_val;
            } else if (r.type == PREPARE) {
                msg prom = { PROMISE, node_id, r.proposal_num, accepted_value };
                send_msg(r.from_id, &prom);
            } else if (r.type == PROMISE && r.proposal_num == highest_proposal) {
                promise_count++;
                if (promise_count > NODES/2 && node_id == leader_id) {
                    msg acc = { ACCEPT, node_id, highest_proposal, PROPOSAL_VALUE };
                    for (int i = 1; i <= NODES; i++) if (i != node_id) send_msg(i, &acc);
                    promise_count = 0;
                }
            } else if (r.type == ACCEPT) {
                accepted_value = r.proposal_val;
                msg accd = { ACCEPTED, node_id, r.proposal_num, accepted_value };
                send_msg(r.from_id, &accd);
            } else if (r.type == ACCEPTED && node_id == leader_id && r.proposal_num == highest_proposal) {
                accepted_count++;
                if (accepted_count > NODES/2) {
                    printf("[Node %d] CONSENSUS on %d\n", node_id, r.proposal_val);
                    accepted_count = 0;
                }
            }
        }
        usleep(100000);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr,"Uso: %s <NODE_ID>\n",argv[0]); return 1; }
    int node_id = atoi(argv[1]);
    queue_init(&inbox);
    pthread_t lt, et, pt;
    pthread_create(&lt, NULL, listener, &node_id);
    pthread_create(&et, NULL, election, &node_id);
    pthread_create(&pt, NULL, paxos, &node_id);
    pthread_join(lt, NULL);
    pthread_join(et, NULL);
    pthread_join(pt, NULL);
    return 0;
}
