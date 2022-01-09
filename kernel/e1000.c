// 82540EM in https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net_buf.h"
#include "net.h"

struct e1000 {
    uint32 mmio_base;
    // buffer for receive
    #define RX_RING_SIZE 16
    struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
    struct mbuf *rx_mbufs[RX_RING_SIZE];

    // buffer for transmit
    #define TX_RING_SIZE 16
    struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
    struct mbuf *tx_mbufs[TX_RING_SIZE];

    // remember where the e1000's registers live.
    volatile uint32 *regs;
    struct netdev *netdev;
    struct spinlock lock;
};

struct e1000 *e1000; 

// called by pciinit().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000init(uint32 *xregs)
{
  int i;
  e1000 = (struct e1000 *)kalloc();

  initlock(&e1000->lock, "e1000");

  e1000->regs = xregs;

  // Reset the device
  e1000->regs[E1000_IMS] = 0; // disable interrupts
  e1000->regs[E1000_CTL] |= E1000_CTL_RST;
  e1000->regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(e1000->tx_ring, 0, sizeof(e1000->tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    e1000->tx_ring[i].status = E1000_TXD_STAT_DD;
    e1000->tx_mbufs[i] = 0;
  }
  e1000->regs[E1000_TDBAL] = (uint64) e1000->tx_ring;
  if(sizeof(e1000->tx_ring) % 128 != 0)
    panic("e1000 tranmit");
  e1000->regs[E1000_TDLEN] = sizeof(e1000->tx_ring);
  e1000->regs[E1000_TDH] = e1000->regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(e1000->rx_ring, 0, sizeof(e1000->rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    e1000->rx_mbufs[i] = mbufalloc(0);
    if (!e1000->rx_mbufs[i])
      panic("e1000 receive");
    e1000->rx_ring[i].addr = (uint64) e1000->rx_mbufs[i]->head;
  }
  e1000->regs[E1000_RDBAL] = (uint64) e1000->rx_ring;
  if(sizeof(e1000->rx_ring) % 128 != 0)
    panic("e1000 receive ring");
  e1000->regs[E1000_RDH] = 0;
  e1000->regs[E1000_RDT] = RX_RING_SIZE - 1;
  e1000->regs[E1000_RDLEN] = sizeof(e1000->rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  e1000->regs[E1000_RA] = 0x12005452;
  e1000->regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    e1000->regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  e1000->regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  e1000->regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  e1000->regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  e1000->regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  e1000->regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  e1000->regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

// add mbuf m to transmit ring(tx_ring), waiting for e1000 to send them
// return -1 for error and 0 for accept
int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000->lock);

  uint32 desc_pos = e1000->regs[E1000_TDT];
  
  // overflow detection
  if((e1000->tx_ring[desc_pos].status & E1000_RXD_STAT_DD) == 0) {
    // you can not use use descriptor
    release(&e1000->lock);
    // return error
    return -1;
  }
  
  if(e1000->tx_mbufs[desc_pos] != 0)
    mbuffree(e1000->tx_mbufs[desc_pos]);

  e1000->tx_mbufs[desc_pos] = m;
  e1000->tx_ring[desc_pos].addr = (uint64) m->head; // not all m->buf is used
  e1000->tx_ring[desc_pos].length = m->len;
  e1000->tx_ring[desc_pos].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

  e1000->regs[E1000_TDT] = (desc_pos + 1) % TX_RING_SIZE;
  __sync_synchronize();
  release(&e1000->lock);
  
  return 0;
}

static void
e1000_recv(void)
{
  struct mbuf *buf;

  uint32 desc_pos = (e1000->regs[E1000_RDT] + 1) % RX_RING_SIZE;

  while((e1000->rx_ring[desc_pos].status & E1000_RXD_STAT_DD)) {
    acquire(&e1000->lock);
    buf = e1000->rx_mbufs[desc_pos];
    mbufput(buf, e1000->rx_ring[desc_pos].length);

    // refill a new mbuf
    e1000->rx_mbufs[desc_pos] = mbufalloc(0);
    if (!e1000->rx_mbufs[desc_pos])
      panic("e1000 receive");
    e1000->rx_ring[desc_pos].addr = (uint64) e1000->rx_mbufs[desc_pos]->head;
    e1000->rx_ring[desc_pos].status = 0;

    e1000->regs[E1000_RDT] = desc_pos;
    __sync_synchronize();
    release(&e1000->lock);

    net_rx(buf);

    desc_pos = (e1000->regs[E1000_RDT] + 1) % RX_RING_SIZE;
  }
}

void
e1000intr(void)
{
  e1000->regs[E1000_ICR] = 0xffffffff;
  e1000_recv();
}