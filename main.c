#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

int main() {
    printf("Compilando client.c, monitor.c e node1-5.c...\n");
    if (system("gcc -o client client.c") != 0){
        fprintf(stderr,"Erro ao compilar client.c\n"); return 1;
    }
    if (system("gcc -o monitor monitor.c") != 0) {
        fprintf(stderr, "Erro ao compilar monitor.c\n"); return 1;
    }
    if (system("gcc -o node1 node1.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node1.c\n"); return 1;
    }
    if (system("gcc -o node2 node2.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node2.c\n"); return 1;
    }
    if (system("gcc -o node3 node3.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node3.c\n"); return 1;
    }
    if (system("gcc -o node4 node4.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node4.c\n"); return 1;
    }
    if (system("gcc -o node5 node5.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node5.c\n"); return 1;
    }

    pid_t mon_pid = fork();
    if (mon_pid == 0) {
        execl("./monitor", "monitor", NULL);
        perror("Falha ao executar monitor");
        exit(1);
    }
    printf("Monitor iniciado (PID %d)\n", mon_pid);
    // da um tempo para o monitor subir
    sleep(1);

    int fail_case = 4;
    char *env = getenv("PAXOS_FAIL_CASE");
    if (env) fail_case = atoi(env);

    pid_t pids[5];
    const char *nodes[] = {"./node1", "./node2", "./node3", "./node4", "./node5"};
    for (int i = 0; i < 5; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char fail_arg[4];
            snprintf(fail_arg, sizeof(fail_arg), "%d", fail_case);
            if (fail_case > 0)
                execl(nodes[i], nodes[i], fail_arg, NULL);
            else
                execl(nodes[i], nodes[i], NULL);
            perror("Falha ao executar node");
            exit(1);
        }
        pids[i] = pid;
        printf("Node %d iniciado (PID %d)\n", i+1, pid);
    }

    sleep(2);

    pid_t client_pid = fork();
    if (client_pid == 0) {
        execl("./client", "client", NULL);
        perror("Falha ao executar client");
        exit(1);
    }
    printf("Client iniciado (PID %d)\n", client_pid);

    waitpid(client_pid, NULL, 0);
    printf("Client finalizado.\n");

    for (int i = 0; i < 5; i++) {
        if (kill(pids[i], 0) == 0) {
            kill(pids[i], SIGTERM);
        }
        waitpid(pids[i], NULL, 0);
        printf("Node %d finalizado.\n", i+1);
    }

    printf("Encerrando monitor...\n");
    if (kill(mon_pid, 0) == 0) {
        kill(mon_pid, SIGINT);
    }
    waitpid(mon_pid, NULL, 0);

    system("./limpar.sh");
    printf("Simulação finalizada. events.csv disponível.\n");
    return 0;
}
