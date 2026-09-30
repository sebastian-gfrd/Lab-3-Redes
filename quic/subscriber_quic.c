#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>

#define QUIC_PORT 7070
#define BROKER_IP "127.0.0.1"
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

static int sockfd = -1;
static uint64_t conn_id = 0;
static struct sockaddr_in broker_addr;

void handle_signal(int sig) {
    (void)sig;
    printf("\n[SUSCRIPTOR QUIC] Cerrando conexión y saliendo...\n");
    if (sockfd >= 0 && conn_id != 0) {
        QuicPacket close_pkt;
        memset(&close_pkt, 0, sizeof(close_pkt));
        close_pkt.header.type = QUIC_PKT_CLOSE;
        close_pkt.header.conn_id = conn_id;
        close_pkt.header.packet_num = 9999;
        sendto(sockfd, &close_pkt, sizeof(close_pkt), 0,
               (struct sockaddr *)&broker_addr, sizeof(broker_addr));
        close(sockfd);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);

    char *topic = "PARTIDO_A";
    if (argc >= 2) {
        topic = argv[1];
    } else {
        printf("Uso opcional: %s <NOMBRE_DEL_PARTIDO>\n", argv[0]);
        printf("Usando tema por defecto: '%s'\n\n", topic);
    }

    // 1. Crear socket UDP para QUIC
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[SUSCRIPTOR QUIC] Error al crear socket UDP");
        exit(EXIT_FAILURE);
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(QUIC_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    socklen_t addrlen = sizeof(broker_addr);
    uint32_t pkt_seq = 1;

    // 2. Handshake QUIC Inicial: Obtener Connection ID (CID)
    printf("[SUSCRIPTOR QUIC] Conectando con Broker en %s:%d...\n", BROKER_IP, QUIC_PORT);

    QuicPacket init_pkt;
    memset(&init_pkt, 0, sizeof(init_pkt));
    init_pkt.header.type = QUIC_PKT_INITIAL;
    init_pkt.header.conn_id = 0;
    init_pkt.header.packet_num = pkt_seq++;

    sendto(sockfd, &init_pkt, sizeof(init_pkt), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    QuicPacket resp_pkt;
    ssize_t bytes = recvfrom(sockfd, &resp_pkt, sizeof(resp_pkt), 0,
                             (struct sockaddr *)&broker_addr, &addrlen);

    if (bytes <= 0 || resp_pkt.header.type != QUIC_PKT_HANDSHAKE) {
        fprintf(stderr, "[SUSCRIPTOR QUIC] Error: No se pudo completar el handshake con el broker.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    conn_id = resp_pkt.header.conn_id;
    printf("[SUSCRIPTOR QUIC] Handshake exitoso. Connection ID: 0x%016llX\n", (unsigned long long)conn_id);

    // 3. Suscripción al tema por Stream 0 (Control)
    QuicPacket sub_pkt;
    memset(&sub_pkt, 0, sizeof(sub_pkt));
    sub_pkt.header.type = QUIC_PKT_1RTT;
    sub_pkt.header.conn_id = conn_id;
    sub_pkt.header.packet_num = pkt_seq++;
    sub_pkt.stream.stream_id = 0; // Stream de Control

    snprintf(sub_pkt.payload, sizeof(sub_pkt.payload), "SUB:%s", topic);
    sub_pkt.stream.length = strlen(sub_pkt.payload);

    sendto(sockfd, &sub_pkt, sizeof(sub_pkt), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    // Esperar confirmación ACK
    QuicPacket ack_pkt;
    recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&broker_addr, &addrlen);
    printf("[SUSCRIPTOR QUIC] Registrado en el tema '%s' por Stream 0. Esperando eventos...\n\n", topic);

    // 4. Ciclo de recepción de eventos por Stream 4 (Datos)
    QuicPacket recv_pkt;
    while (1) {
        memset(&recv_pkt, 0, sizeof(recv_pkt));
        ssize_t b = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                             (struct sockaddr *)&broker_addr, &addrlen);

        if (b < (ssize_t)sizeof(QuicHeader)) {
            continue;
        }

        // Procesar paquetes 1-RTT de datos
        if (recv_pkt.header.type == QUIC_PKT_1RTT && recv_pkt.stream.stream_id == 4) {
            Message *msg = (Message *)recv_pkt.payload;

            printf("[SUSCRIPTOR QUIC] [Stream 4 | Pkt #%u] -> [%s] Evento #%d: %s\n",
                   recv_pkt.header.packet_num, msg->topic, msg->seq_num, msg->content);

            // Responder con ACK al broker para confirmar recepción
            QuicPacket confirm_ack;
            memset(&confirm_ack, 0, sizeof(confirm_ack));
            confirm_ack.header.type = QUIC_PKT_ACK;
            confirm_ack.header.conn_id = conn_id;
            confirm_ack.header.packet_num = pkt_seq++;
            confirm_ack.header.ack_num = recv_pkt.header.packet_num;

            sendto(sockfd, &confirm_ack, sizeof(confirm_ack), 0,
                   (struct sockaddr *)&broker_addr, sizeof(broker_addr));
        }
    }

    close(sockfd);
    return 0;
}
