#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

#define QUIC_PORT 7070
#define MAX_CLIENTS 100
#define MAX_PAYLOAD_SIZE 1024

// Estructura de Mensaje idéntica a TCP y UDP
typedef struct {
    char topic[32];
    char content[256];
    int seq_num;
} Message;

// Tipos de paquetes QUIC (basados en conceptos de RFC 9000)
typedef enum {
    QUIC_PKT_INITIAL   = 0x01, // Handshake inicial
    QUIC_PKT_HANDSHAKE = 0x02, // Confirmación de Handshake
    QUIC_PKT_1RTT      = 0x03, // Datos de aplicación en Streams
    QUIC_PKT_ACK       = 0x04, // Confirmación de entrega
    QUIC_PKT_CLOSE     = 0x05  // Cierre de conexión
} QuicPacketType;

// Encabezado QUIC con Connection ID (CID) y número de paquete
typedef struct {
    uint8_t  type;
    uint64_t conn_id;
    uint32_t packet_num;
    uint32_t ack_num;
} QuicHeader;

// Frame de Stream multiplexado (Stream 0: Control, Stream 4: Datos)
typedef struct {
    uint32_t stream_id;
    uint32_t offset;
    uint32_t length;
    uint8_t  fin;
} QuicStreamFrame;

// Datagrama QUIC sobre socket UDP
typedef struct {
    QuicHeader header;
    QuicStreamFrame stream;
    char payload[MAX_PAYLOAD_SIZE];
} QuicPacket;

// Registro de Suscriptores QUIC
typedef struct {
    uint64_t conn_id;
    struct sockaddr_in addr;
    char topic[32];
    int active;
} QuicSubscriber;

static QuicSubscriber subscribers[MAX_CLIENTS];
static int sub_count = 0;
static int server_fd = -1;
static uint32_t broker_pkt_seq = 1000;

// Generador de Connection IDs únicos de 64 bits (RFC 9000)
static uint64_t generate_connection_id() {
    static uint32_t counter = 1;
    uint64_t high = (uint64_t)time(NULL) & 0xFFFFFFFF;
    uint64_t low = ((uint64_t)rand() << 16) ^ (counter++);
    return (high << 32) | low;
}

void handle_signal(int sig) {
    (void)sig;
    printf("\n[BROKER QUIC] Apagando el broker limpiamente...\n");
    if (server_fd >= 0) {
        close(server_fd);
    }
    exit(0);
}

int main() {
    signal(SIGINT, handle_signal);
    srand(time(NULL));

    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    // 1. Crear socket UDP como transporte subyacente para QUIC (RFC 9000)
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[BROKER QUIC] Error creando socket UDP");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(QUIC_PORT);

    // 2. Asociar el socket al puerto asignado
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[BROKER QUIC] Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("=========================================================\n");
    printf("[BROKER QUIC] Iniciado y escuchando en el puerto %d (UDP)\n", QUIC_PORT);
    printf("[BROKER QUIC] Soporte para Connection IDs y Streams multiplexados\n");
    printf("=========================================================\n\n");

    QuicPacket recv_pkt;

    while (1) {
        memset(&recv_pkt, 0, sizeof(recv_pkt));
        ssize_t bytes = recvfrom(server_fd, &recv_pkt, sizeof(recv_pkt), 0,
                                 (struct sockaddr *)&client_addr, &addrlen);

        if (bytes < (ssize_t)sizeof(QuicHeader)) {
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        // 3. Procesar tipos de paquetes QUIC
        switch (recv_pkt.header.type) {
            case QUIC_PKT_INITIAL: {
                // Handshake inicial QUIC: Cliente solicita un Connection ID (CID)
                uint64_t assigned_cid = generate_connection_id();
                printf("[BROKER QUIC] [HANDSHAKE INITIAL] Recibido de %s:%d -> Asignando CID: 0x%016llX\n",
                       client_ip, client_port, (unsigned long long)assigned_cid);

                QuicPacket response_pkt;
                memset(&response_pkt, 0, sizeof(response_pkt));
                response_pkt.header.type = QUIC_PKT_HANDSHAKE;
                response_pkt.header.conn_id = assigned_cid;
                response_pkt.header.packet_num = broker_pkt_seq++;
                response_pkt.header.ack_num = recv_pkt.header.packet_num;

                sendto(server_fd, &response_pkt, sizeof(response_pkt), 0,
                       (struct sockaddr *)&client_addr, sizeof(client_addr));
                break;
            }

            case QUIC_PKT_1RTT: {
                // Paquete de datos en un Stream específico
                uint32_t stream_id = recv_pkt.stream.stream_id;

                // Stream 0: Canal de Control (Suscripciones SUB y registros PUB)
                if (stream_id == 0) {
                    if (strncmp(recv_pkt.payload, "SUB:", 4) == 0) {
                        char topic[32];
                        strncpy(topic, recv_pkt.payload + 4, sizeof(topic) - 1);
                        topic[sizeof(topic) - 1] = '\0';
                        topic[strcspn(topic, "\r\n")] = '\0';

                        // Registrar o actualizar suscriptor
                        int found = -1;
                        for (int i = 0; i < sub_count; i++) {
                            if (subscribers[i].conn_id == recv_pkt.header.conn_id) {
                                found = i;
                                break;
                            }
                        }

                        if (found == -1 && sub_count < MAX_CLIENTS) {
                            found = sub_count++;
                        }

                        if (found != -1) {
                            subscribers[found].conn_id = recv_pkt.header.conn_id;
                            subscribers[found].addr = client_addr;
                            strncpy(subscribers[found].topic, topic, sizeof(subscribers[found].topic));
                            subscribers[found].active = 1;

                            printf("[BROKER QUIC] [STREAM 0] Suscriptor registrado -> CID: 0x%016llX | Tema: %s | Cliente: %s:%d\n",
                                   (unsigned long long)recv_pkt.header.conn_id, topic, client_ip, client_port);
                        }

                        // Enviar confirmación ACK de registro
                        QuicPacket ack_pkt;
                        memset(&ack_pkt, 0, sizeof(ack_pkt));
                        ack_pkt.header.type = QUIC_PKT_ACK;
                        ack_pkt.header.conn_id = recv_pkt.header.conn_id;
                        ack_pkt.header.ack_num = recv_pkt.header.packet_num;
                        sendto(server_fd, &ack_pkt, sizeof(ack_pkt), 0,
                               (struct sockaddr *)&client_addr, sizeof(client_addr));
                    } else if (strncmp(recv_pkt.payload, "PUB", 3) == 0) {
                        printf("[BROKER QUIC] [STREAM 0] Publicador registrado -> CID: 0x%016llX (%s:%d)\n",
                               (unsigned long long)recv_pkt.header.conn_id, client_ip, client_port);

                        // Enviar confirmación ACK
                        QuicPacket ack_pkt;
                        memset(&ack_pkt, 0, sizeof(ack_pkt));
                        ack_pkt.header.type = QUIC_PKT_ACK;
                        ack_pkt.header.conn_id = recv_pkt.header.conn_id;
                        ack_pkt.header.ack_num = recv_pkt.header.packet_num;
                        sendto(server_fd, &ack_pkt, sizeof(ack_pkt), 0,
                               (struct sockaddr *)&client_addr, sizeof(client_addr));
                    }
                }
                // Stream 4: Canal de Eventos Deportivos (Datos de aplicación)
                else if (stream_id == 4) {
                    // Enviar ACK inmediato al publicador para garantizar entrega confiable
                    QuicPacket ack_pkt;
                    memset(&ack_pkt, 0, sizeof(ack_pkt));
                    ack_pkt.header.type = QUIC_PKT_ACK;
                    ack_pkt.header.conn_id = recv_pkt.header.conn_id;
                    ack_pkt.header.ack_num = recv_pkt.header.packet_num;
                    sendto(server_fd, &ack_pkt, sizeof(ack_pkt), 0,
                           (struct sockaddr *)&client_addr, sizeof(client_addr));

                    Message *msg = (Message *)recv_pkt.payload;
                    printf("[BROKER QUIC] [STREAM 4] Mensaje recibido [Pkt #%u | CID: 0x%016llX] -> [%s] #%d: %s\n",
                           recv_pkt.header.packet_num,
                           (unsigned long long)recv_pkt.header.conn_id,
                           msg->topic, msg->seq_num, msg->content);

                    // Reenviar a los suscriptores registrados al tema
                    int forwarded = 0;
                    for (int i = 0; i < sub_count; i++) {
                        if (subscribers[i].active && strcmp(subscribers[i].topic, msg->topic) == 0) {
                            QuicPacket forward_pkt;
                            memset(&forward_pkt, 0, sizeof(forward_pkt));
                            forward_pkt.header.type = QUIC_PKT_1RTT;
                            forward_pkt.header.conn_id = subscribers[i].conn_id;
                            forward_pkt.header.packet_num = broker_pkt_seq++;

                            forward_pkt.stream.stream_id = 4; // Mismo Stream de datos
                            forward_pkt.stream.offset = 0;
                            forward_pkt.stream.length = sizeof(Message);
                            forward_pkt.stream.fin = 0;
                            memcpy(forward_pkt.payload, msg, sizeof(Message));

                            sendto(server_fd, &forward_pkt, sizeof(forward_pkt), 0,
                                   (struct sockaddr *)&subscribers[i].addr, sizeof(subscribers[i].addr));
                            forwarded++;
                        }
                    }
                    printf("             └── Reenviado a %d suscriptores del tema '%s'\n", forwarded, msg->topic);
                }
                break;
            }

            case QUIC_PKT_ACK: {
                // Manejo de confirmación ACK desde un suscriptor
                break;
            }

            case QUIC_PKT_CLOSE: {
                printf("[BROKER QUIC] Cierre de conexión solicitado para CID: 0x%016llX\n",
                       (unsigned long long)recv_pkt.header.conn_id);
                for (int i = 0; i < sub_count; i++) {
                    if (subscribers[i].conn_id == recv_pkt.header.conn_id) {
                        subscribers[i].active = 0;
                        break;
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    close(server_fd);
    return 0;
}
