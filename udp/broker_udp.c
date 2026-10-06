#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9090
#define MAX_CLIENTS 100

typedef struct {
    char topic[32];
    char content[256];
    int seq_num;
} Message;

// En UDP guardamos la dirección (IP + puerto)
typedef struct {
    struct sockaddr_in addr;
    char topic[32];
} Subscriber;

Subscriber subscribers[MAX_CLIENTS];
int sub_count = 0;

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    int server_fd;
    struct sockaddr_in address, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    // 1. Crear socket UDP (SOCK_DGRAM)
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error al crear el socket UDP");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // 2. Asociar el socket al puerto local
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    printf("[BROKER UDP] Escuchando en el puerto %d...\n", port);

    char buffer[512];

    // Bucle principal
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recvfrom(server_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addrlen);
        if (bytes <= 0) continue;

        // Comprobación de Suscripción
        if (strncmp(buffer, "SUB:", 4) == 0) {
            char topic_buf[32];
            size_t topic_len = sizeof(topic_buf) - 1;
            memcpy(topic_buf, buffer + 4, topic_len);
            topic_buf[topic_len] = '\0';
            topic_buf[strcspn(topic_buf, "\r\n")] = 0;

            int found = -1;
            for (int i = 0; i < sub_count; i++) {
                if (subscribers[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                    subscribers[i].addr.sin_port == client_addr.sin_port) {
                    found = i;
                    break;
                }
            }

            if (found != -1) {
                memcpy(subscribers[found].topic, topic_buf, sizeof(subscribers[found].topic));
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
                printf("[BROKER UDP] Suscriptor actualizado -> Tema: %s (%s:%d)\n",
                       subscribers[found].topic, ip, ntohs(client_addr.sin_port));
            } else if (sub_count < MAX_CLIENTS) {
                subscribers[sub_count].addr = client_addr;
                memcpy(subscribers[sub_count].topic, topic_buf, sizeof(subscribers[sub_count].topic));

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
                printf("[BROKER UDP] Suscriptor registrado -> Tema: %s (%s:%d)\n", 
                       subscribers[sub_count].topic, ip, ntohs(client_addr.sin_port));
                
                sub_count++;
            }
        } 
        // Comprobación de Publicación
        else if (strncmp(buffer, "PUB", 3) == 0) {
            if (bytes < 3 + (int)sizeof(Message)) {
                continue;
            }
            Message msg;
            memcpy(&msg, buffer + 3, sizeof(Message));
            printf("[BROKER UDP] Mensaje recibido -> [%s] #%d: %s\n", msg.topic, msg.seq_num, msg.content);

            // Reenviar a todos los suscriptores del tema correspondiente
            for (int i = 0; i < sub_count; i++) {
                if (strcmp(subscribers[i].topic, msg.topic) == 0) {
                    sendto(server_fd, &msg, sizeof(Message), 0,
                           (struct sockaddr *)&subscribers[i].addr, sizeof(subscribers[i].addr));
                }
            }
        }
    }

    close(server_fd);
    return 0;
}