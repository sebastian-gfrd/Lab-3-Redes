#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdint.h>

#define QUIC_PORT 7070
#define BROKER_IP "127.0.0.1"
#define MAX_PAYLOAD_SIZE 1024
#define MAX_RETRIES 3

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

int main(int argc, char *argv[]) {
    char *topic = "PARTIDO_A";
    if (argc >= 2) {
        topic = argv[1];
    } else {
        printf("Uso opcional: %s <NOMBRE_DEL_PARTIDO>\n", argv[0]);
        printf("Usando tema por defecto: '%s'\n\n", topic);
    }

    int sockfd;
    struct sockaddr_in broker_addr;

    // 1. Crear socket UDP para la capa de transporte
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[PUBLICADOR QUIC] Error al crear socket UDP");
        exit(EXIT_FAILURE);
    }

    // Configurar timeout de recepción para gestionar ACKs y retransmisiones
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(QUIC_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    uint32_t pkt_seq = 1;
    uint64_t conn_id = 0;

    // 2. Handshake QUIC Inicial: Obtener Connection ID (CID)
    printf("[PUBLICADOR QUIC] Iniciando Handshake QUIC con Broker en %s:%d...\n", BROKER_IP, QUIC_PORT);
    QuicPacket init_pkt;
    memset(&init_pkt, 0, sizeof(init_pkt));
    init_pkt.header.type = QUIC_PKT_INITIAL;
    init_pkt.header.conn_id = 0;
    init_pkt.header.packet_num = pkt_seq++;

    int handshake_ok = 0;
    socklen_t addrlen = sizeof(broker_addr);

    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        sendto(sockfd, &init_pkt, sizeof(init_pkt), 0,
               (struct sockaddr *)&broker_addr, sizeof(broker_addr));

        QuicPacket resp_pkt;
        ssize_t bytes = recvfrom(sockfd, &resp_pkt, sizeof(resp_pkt), 0,
                                 (struct sockaddr *)&broker_addr, &addrlen);

        if (bytes > 0 && resp_pkt.header.type == QUIC_PKT_HANDSHAKE) {
            conn_id = resp_pkt.header.conn_id;
            handshake_ok = 1;
            printf("[PUBLICADOR QUIC] Handshake completado exitosamente.\n");
            printf("[PUBLICADOR QUIC] Connection ID asignado: 0x%016llX\n\n", (unsigned long long)conn_id);
            break;
        }
        printf("[PUBLICADOR QUIC] Reintentando Handshake inicial (%d/%d)...\n", retry + 1, MAX_RETRIES);
    }

    if (!handshake_ok) {
        fprintf(stderr, "[PUBLICADOR QUIC] Error: No se pudo conectar al Broker QUIC.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 3. Registro de rol en Stream 0 (Canal de Control)
    QuicPacket reg_pkt;
    memset(&reg_pkt, 0, sizeof(reg_pkt));
    reg_pkt.header.type = QUIC_PKT_1RTT;
    reg_pkt.header.conn_id = conn_id;
    reg_pkt.header.packet_num = pkt_seq++;
    reg_pkt.stream.stream_id = 0; // Stream de Control
    strcpy(reg_pkt.payload, "PUB");
    reg_pkt.stream.length = strlen(reg_pkt.payload);

    sendto(sockfd, &reg_pkt, sizeof(reg_pkt), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    QuicPacket ack_pkt;
    recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&broker_addr, &addrlen);
    printf("[PUBLICADOR QUIC] Rol registrado por Stream 0 (Control). ACK recibido.\n\n");

    // 4. Envío de 10 eventos deportivos en Stream 4 (Multiplexación y Entrega Confiable)
    printf("[PUBLICADOR QUIC] Transmitiendo 10 eventos para '%s' en Stream #4...\n", topic);

    for (int i = 1; i <= 10; i++) {
        Message msg;
        strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
        msg.topic[sizeof(msg.topic) - 1] = '\0';
        msg.seq_num = i;
        snprintf(msg.content, sizeof(msg.content), "Gol anotado en %s - Minuto %d", topic, i * 8);

        QuicPacket data_pkt;
        memset(&data_pkt, 0, sizeof(data_pkt));
        data_pkt.header.type = QUIC_PKT_1RTT;
        data_pkt.header.conn_id = conn_id;
        data_pkt.header.packet_num = pkt_seq++;
        data_pkt.stream.stream_id = 4; // Stream multiplexado para datos de eventos
        data_pkt.stream.offset = (i - 1) * sizeof(Message);
        data_pkt.stream.length = sizeof(Message);
        data_pkt.stream.fin = (i == 10) ? 1 : 0;
        memcpy(data_pkt.payload, &msg, sizeof(Message));

        // Enviar con soporte de retransmisión ante pérdida de paquetes (Confiabilidad QUIC)
        int ack_received = 0;
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            sendto(sockfd, &data_pkt, sizeof(data_pkt), 0,
                   (struct sockaddr *)&broker_addr, sizeof(broker_addr));

            ssize_t b = recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
                                 (struct sockaddr *)&broker_addr, &addrlen);

            if (b > 0 && ack_pkt.header.type == QUIC_PKT_ACK &&
                ack_pkt.header.ack_num == data_pkt.header.packet_num) {
                ack_received = 1;
                printf("[PUBLICADOR QUIC] [Stream 4 | Pkt #%u] Enviado evento #%d: '%s' (ACK OK)\n",
                       data_pkt.header.packet_num, i, msg.content);
                break;
            } else {
                printf("[PUBLICADOR QUIC] Timeout esperando ACK para Pkt #%u. Reintentando...\n",
                       data_pkt.header.packet_num);
            }
        }

        if (!ack_received) {
            fprintf(stderr, "[PUBLICADOR QUIC] Advertencia: No se recibió ACK para evento #%d.\n", i);
        }

        sleep(1); // Retardo entre envíos para simular tiempo real
    }

    printf("\n[PUBLICADOR QUIC] Finalizó la transmisión de los 10 eventos.\n");

    // 5. Cierre ordenado de la conexión QUIC
    QuicPacket close_pkt;
    memset(&close_pkt, 0, sizeof(close_pkt));
    close_pkt.header.type = QUIC_PKT_CLOSE;
    close_pkt.header.conn_id = conn_id;
    close_pkt.header.packet_num = pkt_seq++;
    sendto(sockfd, &close_pkt, sizeof(close_pkt), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    close(sockfd);
    return 0;
}
