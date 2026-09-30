CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lpthread

TCP_TARGETS = tcp/broker_tcp tcp/publisher_tcp tcp/subscriber_tcp
UDP_TARGETS = udp/broker_udp udp/publisher_udp udp/subscriber_udp
QUIC_TARGETS = quic/broker_quic quic/publisher_quic quic/subscriber_quic

.PHONY: all tcp udp quic clean

all: tcp udp quic

tcp: $(TCP_TARGETS)

udp: $(UDP_TARGETS)

quic: $(QUIC_TARGETS)

# Reglas de compilación TCP
tcp/broker_tcp: tcp/broker_tcp.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

tcp/publisher_tcp: tcp/publisher_tcp.c
	$(CC) $(CFLAGS) $< -o $@

tcp/subscriber_tcp: tcp/subscriber_tcp.c
	$(CC) $(CFLAGS) $< -o $@

# Reglas de compilación UDP
udp/broker_udp: udp/broker_udp.c
	$(CC) $(CFLAGS) $< -o $@

udp/publisher_udp: udp/publisher_udp.c
	$(CC) $(CFLAGS) $< -o $@

udp/subscriber_udp: udp/subscriber_udp.c
	$(CC) $(CFLAGS) $< -o $@

# Reglas de compilación QUIC
quic/broker_quic: quic/broker_quic.c
	$(CC) $(CFLAGS) $< -o $@

quic/publisher_quic: quic/publisher_quic.c
	$(CC) $(CFLAGS) $< -o $@

quic/subscriber_quic: quic/subscriber_quic.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TCP_TARGETS) $(UDP_TARGETS) $(QUIC_TARGETS)
