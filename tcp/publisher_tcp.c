#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

typedef struct {
    char topic[32];
    char content[256];
    int seq_num;
} Message;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <NOMBRE_DEL_PARTIDO>\n", argv[0]);
        printf("Ejemplo: %s PARTIDO_A\n", argv[0]);
        return 1;
    }

    char *topic = argv[1];
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creando socket");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Dirección inválida");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Conexión fallida al broker");
        return -1;
    }

    write(sock, "PUB", 3);
    sleep(1);

    printf("[PUBLICADOR] Transmitiendo 10 eventos para '%s'...\n", topic);

    for (int i = 1; i <= 10; i++) {
        Message msg;
        strncpy(msg.topic, topic, sizeof(msg.topic));
        msg.seq_num = i;
        snprintf(msg.content, sizeof(msg.content), "Evento %d: Actualización de min %d en %s", i, i * 8, topic);

        write(sock, &msg, sizeof(Message));
        printf(" [ENVIADO] Evento #%d\n", i);
        sleep(1);
    }

    printf("[PUBLICADOR] Finalizó el envío de 10 mensajes.\n");
    close(sock);
    return 0;
}
