#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

int main() {
    // 1) Compila o monitor e o node
    printf("Compilando monitor.c e node.c...\n");
    if (system("gcc -o monitor monitor.c") != 0) {
        fprintf(stderr, "Erro ao compilar monitor.c\n"); return 1;
    }
    if (system("gcc -o node node.c -lpthread") != 0) {
        fprintf(stderr, "Erro ao compilar node.c\n"); return 1;
    }

    // 2) Inicia o monitor
    pid_t mon_pid = fork();
    if (mon_pid == 0) {
        execl("./monitor", "monitor", NULL);
        perror("Falha ao executar monitor");
        exit(1);
    }
    // dá um tempo para o monitor subir
    sleep(1);

    // 3) Inicia 5 nós
    pid_t pids[5];
    for (int i = 1; i <= 5; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char arg[4];
            snprintf(arg, sizeof(arg), "%d", i);
            execl("./node", "node", arg, NULL);
            perror("Falha ao executar node");
            exit(1);
        }
        pids[i-1] = pid;
    }

    // 4) Aguarda término dos nós
    for (int i = 0; i < 5; i++) {
        waitpid(pids[i], NULL, 0);
    }

    // 5) Encerra o monitor
    printf("Encerrando monitor...\n");
    kill(mon_pid, SIGINT);
    waitpid(mon_pid, NULL, 0);

    printf("Simulação finalizada. events.csv disponível.\n");
    return 0;
}
