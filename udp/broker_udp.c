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

int main() {
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
    address.sin_port = htons(PORT);

    // 2. Asociar el socket al puerto local
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    printf("[BROKER UDP] Escuchando en el puerto %d...\n", PORT);

    char buffer[512];

    // Bucle principal
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recvfrom(server_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addrlen);
        if (bytes <= 0) continue;

        // Comprobación de Suscripción
        if (strncmp(buffer, "SUB:", 4) == 0) {
            if (sub_count < MAX_CLIENTS) {
                subscribers[sub_count].addr = client_addr;
                strncpy(subscribers[sub_count].topic, buffer + 4, 31);
                subscribers[sub_count].topic[strcspn(subscribers[sub_count].topic, "\r\n")] = 0;

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
                printf("[BROKER UDP] Suscriptor registrado -> Tema: %s (%s:%d)\n", 
                       subscribers[sub_count].topic, ip, ntohs(client_addr.sin_port));
                
                sub_count++;
            }
        } 
        // Comprobación de Publicación
        else if (strncmp(buffer, "PUB", 3) == 0) {
            // El mensaje útil viene inmediatamente después del encabezado "PUB"
            Message *msg = (Message *)(buffer + 3);
            printf("[BROKER UDP] Mensaje recibido -> [%s] #%d: %s\n", msg->topic, msg->seq_num, msg->content);

            // Reenviar a todos los suscriptores del tema correspondiente
            for (int i = 0; i < sub_count; i++) {
                if (strcmp(subscribers[i].topic, msg->topic) == 0) {
                    sendto(server_fd, msg, sizeof(Message), 0, 
                           (struct sockaddr *)&subscribers[i].addr, sizeof(subscribers[i].addr));
                }
            }
        }
    }

    close(server_fd);
    return 0;
}