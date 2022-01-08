#include "types.h"
#include "param.h"
#include "net.h"
#include "net_buf.h"

// static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // qemu's idea of the guest IP
// static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
// static uint8 broadcast_mac[ETHADDR_LEN] = { 0xFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF };

// receive a packet from mbuf
// dispatch to some network layer protocol (etc. ip or arp)
void net_rx(struct mbuf * m)
{

}