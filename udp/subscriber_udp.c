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

int main(int argc, char *argv[]) {
    // Verificar si el usuario ingresó el tema por línea de comandos
    if (argc < 2) {
        printf("Uso: %s <NOMBRE_DEL_PARTIDO> [PUERTO] [IP]\n", argv[0]);
        printf("Ejemplo: %s PARTIDO_A 9090 127.0.0.1\n", argv[0]);
        return 1;
    }

    char *topic = argv[1]; // Guardar el partido ingresado
    int port = (argc >= 3) ? atoi(argv[2]) : BROKER_PORT;
    const char *ip = (argc >= 4) ? argv[3] : BROKER_IP;

    int sockfd;
    struct sockaddr_in broker_addr;
    Message msg;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear socket UDP");
        return -1;
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(port);
    broker_addr.sin_addr.s_addr = inet_addr(ip);

    // Formatear mensaje de suscripción: "SUB:PARTIDO_A"
    char sub_req[64];
    snprintf(sub_req, sizeof(sub_req), "SUB:%s", topic);

    // Enviar solicitud de suscripción al broker por UDP
    sendto(sockfd, sub_req, strlen(sub_req), 0, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
    printf("[SUSCRIPTOR UDP] Registrado en el tema '%s'. Esperando eventos...\n", topic);

    // Bucle para recibir mensajes del broker
    while (1) {
        socklen_t len = sizeof(broker_addr);
        int bytes = recvfrom(sockfd, &msg, sizeof(Message), 0, (struct sockaddr *)&broker_addr, &len);
        if (bytes > 0) {
            printf(" -> [%s] Evento #%d: %s\n", msg.topic, msg.seq_num, msg.content);
        }
    }

    close(sockfd);
    return 0;
}