#include "net_buf.h"

struct spinlock;
struct mbufq;


struct sock {
  struct sock *next; // the next socket in the list
  uint8 ip_type;     // ip procotol type
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  uint8 icmp_type;
  uint8 icmp_code;
  uint8 icmp_recvttl;
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};