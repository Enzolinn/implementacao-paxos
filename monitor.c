// (igual ao anterior, mas grava CSV header antes)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MONITOR_PORT 6000
#define BUF_SIZE 512

int main() {
    FILE *csv = fopen("events.csv","w");
    fprintf(csv,"timestamp,source,destination,action,proposal_num,proposal_val\n");
    fclose(csv);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MONITOR_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(sock,(struct sockaddr*)&addr,sizeof(addr));
    char buf[BUF_SIZE];
    while (1) {
        ssize_t n = recvfrom(sock, buf, BUF_SIZE-1, 0, NULL, NULL);
        if (n>0) {
            buf[n]='\0';
            FILE *f = fopen("events.csv","a");
            fprintf(f,"%s", buf);
            fclose(f);
            // printf("%s", buf);  // opcional no console
        }
    }
    return 0;
}
