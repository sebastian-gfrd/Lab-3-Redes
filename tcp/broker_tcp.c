#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 100

typedef struct {
    char topic[32];
    char content[256];
    int seq_num;
} Message;

typedef struct {
    int socket_fd;
    char topic[32];
} Subscriber;

Subscriber subscribers[MAX_CLIENTS];
int sub_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static ssize_t read_all(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < count) {
        ssize_t n = read(fd, p + total, count - total);
        if (n <= 0) return n;
        total += n;
    }
    return (ssize_t)total;
}

static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < count) {
        ssize_t n = write(fd, p + total, count - total);
        if (n <= 0) return n;
        total += n;
    }
    return (ssize_t)total;
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char role_buffer[64];
    memset(role_buffer, 0, sizeof(role_buffer));

    if (read(client_fd, role_buffer, sizeof(role_buffer) - 1) <= 0) {
        close(client_fd);
        return NULL;
    }

    if (strncmp(role_buffer, "SUB:", 4) == 0) {
        pthread_mutex_lock(&clients_mutex);
        if (sub_count < MAX_CLIENTS) {
            subscribers[sub_count].socket_fd = client_fd;
            size_t topic_len = sizeof(subscribers[sub_count].topic) - 1;
            memcpy(subscribers[sub_count].topic, role_buffer + 4, topic_len);
            subscribers[sub_count].topic[topic_len] = '\0';
            subscribers[sub_count].topic[strcspn(subscribers[sub_count].topic, "\r\n")] = 0;
            printf("[BROKER] Suscriptor registrado en tema: %s (fd: %d)\n", subscribers[sub_count].topic, client_fd);
            sub_count++;
        }
        pthread_mutex_unlock(&clients_mutex);

        int connected = 1;
        while (connected) {
            char dummy;
            ssize_t r = recv(client_fd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
            if (r == 0 || (r < 0 && (errno != EAGAIN && errno != EWOULDBLOCK))) {
                connected = 0;
            } else {
                sleep(1);
            }
        }

        // Cleanup subscriber on disconnect
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < sub_count; i++) {
            if (subscribers[i].socket_fd == client_fd) {
                printf("[BROKER] Suscriptor desconectado en tema: %s (fd: %d)\n", subscribers[i].topic, client_fd);
                subscribers[i] = subscribers[sub_count - 1];
                sub_count--;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

    } else if (strncmp(role_buffer, "PUB", 3) == 0) {
        printf("[BROKER] Publicador conectado (fd: %d)\n", client_fd);
        Message msg;
        while (read_all(client_fd, &msg, sizeof(Message)) == (ssize_t)sizeof(Message)) {
            printf("[BROKER] Mensaje recibido -> [%s] #%d: %s\n", msg.topic, msg.seq_num, msg.content);

            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < sub_count; i++) {
                if (strcmp(subscribers[i].topic, msg.topic) == 0) {
                    if (write_all(subscribers[i].socket_fd, &msg, sizeof(Message)) <= 0) {
                        perror("[BROKER] Error enviando mensaje a suscriptor");
                    }
                }
            }
            pthread_mutex_unlock(&clients_mutex);
        }
    }

    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    int server_fd, *new_sock;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket falló");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind falló");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen falló");
        exit(EXIT_FAILURE);
    }

    printf("[BROKER TCP] Escuchando en el puerto %d...\n", port);

    int running = 1;
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

        if (client_fd >= 0) {
            pthread_t thread_id;
            new_sock = malloc(sizeof(int));
            *new_sock = client_fd;
            pthread_create(&thread_id, NULL, handle_client, (void *)new_sock);
            pthread_detach(thread_id);
        }
    }

    close(server_fd);
    return 0;
}
