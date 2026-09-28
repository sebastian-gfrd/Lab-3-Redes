#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BROKER_PORT 9090
#define BROKER_IP "127.0.0.1"

typedef struct {
    char topic[32];
    char content[256];
    int seq_num;
} Message;

int main() {
    int sockfd;
    struct sockaddr_in broker_addr;
    Message msg;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    // Enviar cadena de registro con formato "SUB:<topic>"
    char sub_req[64] = "SUB:PARTIDO_A";
    sendto(sockfd, sub_req, strlen(sub_req), 0, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
    printf("[SUSCRIPTOR UDP] Registrado en %s. Esperando eventos...\n", sub_req + 4);

    while (1) {
        socklen_t len = sizeof(broker_addr);
        int bytes = recvfrom(sockfd, &msg, sizeof(Message), 0, (struct sockaddr *)&broker_addr, &len);
        if (bytes > 0) {
            printf("[SUSCRIPTOR] Recibido -> [%s] Evento #%d: %s\n", msg.topic, msg.seq_num, msg.content);
        }
    }

    close(sockfd);
    return 0;
}