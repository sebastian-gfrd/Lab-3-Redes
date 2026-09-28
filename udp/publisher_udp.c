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

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    char buffer[512];
    // Encabezado "PUB"
    memcpy(buffer, "PUB", 3);

    Message msg;
    strcpy(msg.topic, "PARTIDO_A");

    // Requisito del laboratorio: al menos 10 mensajes
    for (int i = 1; i <= 10; i++) {
        msg.seq_num = i;
        snprintf(msg.content, sizeof(msg.content), "Gol anotado en %s - Minuto %d", msg.topic, i * 8);

        // Se adosa la estructura después del prefijo "PUB"
        memcpy(buffer + 3, &msg, sizeof(Message));

        sendto(sockfd, buffer, 3 + sizeof(Message), 0, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
        printf("[PUBLICADOR] Enviado mensaje #%d para %s\n", i, msg.topic);
        sleep(1); // Retardo entre envíos
    }

    close(sockfd);
    return 0;
}