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

    char sub_req[64];
    snprintf(sub_req, sizeof(sub_req), "SUB:%s", topic);
    write(sock, sub_req, strlen(sub_req));

    printf("[SUSCRIPTOR] Registrado en el tema '%s'. Esperando noticias...\n", topic);

    Message msg;
    
    while (read(sock, &msg, sizeof(Message)) > 0) {
        printf(" -> [%s] Msg #%d: %s\n", msg.topic, msg.seq_num, msg.content);
    }

    printf("[SUSCRIPTOR] Conexión cerrada con el broker.\n");
    close(sock);
    return 0;
}
