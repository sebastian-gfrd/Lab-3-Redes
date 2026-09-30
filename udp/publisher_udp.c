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
    // 1. Validar que el usuario ingrese el partido como argumento
    if (argc < 2) {
        printf("Uso: %s <NOMBRE_DEL_PARTIDO>\n", argv[0]);
        printf("Ejemplo: %s PARTIDO_A\n", argv[0]);
        printf("Ejemplo: %s PARTIDO_B\n", argv[0]);
        return 1;
    }

    // Capturar el tema/partido desde la línea de comandos
    char *topic = argv[1];

    int sockfd;
    struct sockaddr_in broker_addr;

    // 2. Crear socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    char buffer[512];
    // Encabezado "PUB" obligatorio para el Broker
    memcpy(buffer, "PUB", 3);

    Message msg;
    // Asignar dinámicamente el partido ingresado por el usuario
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';

    printf("[PUBLICADOR UDP] Iniciando transmision para el tema: '%s'...\n", msg.topic);

    // Requisito del laboratorio: enviar al menos 10 mensajes
    for (int i = 1; i <= 10; i++) {
        msg.seq_num = i;
        snprintf(msg.content, sizeof(msg.content), "Gol anotado en %s - Minuto %d", msg.topic, i * 8);

        // Se adosa la estructura Message después del prefijo "PUB" (3 bytes)
        memcpy(buffer + 3, &msg, sizeof(Message));

        sendto(sockfd, buffer, 3 + sizeof(Message), 0, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
        printf("[PUBLICADOR] Enviado mensaje #%d para %s\n", i, msg.topic);
        
        sleep(1); // Retardo de 1 segundo entre envíos
    }

    close(sockfd);
    return 0;
}