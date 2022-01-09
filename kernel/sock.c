//
// network system calls.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"
#include "sock.h"

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
udp_sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->ip_type = IPPROTO_UDP;
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

int
icmp_sockalloc(struct file **f, uint32 raddr, uint8 icmp_type, uint8 icmp_code){
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->ip_type = IPPROTO_ICMP;
  si->raddr = raddr;
  si->icmp_type = icmp_type;
  si->icmp_code = icmp_code;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->ip_type == IPPROTO_ICMP && 
    pos->raddr == raddr) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

int sockalloc(struct file **f, uint8 ip_type, ...){
  va_list ap;
  va_start(ap, ip_type);
  if(ip_type == IPPROTO_UDP) {
    uint32 raddr = va_arg(ap,uint32);
    uint16 lport = va_arg(ap,uint32);
    uint16 rport = va_arg(ap,uint32);
    return udp_sockalloc(f, raddr, lport, rport);
  }
  else if(ip_type == IPPROTO_ICMP) {
    uint32 raddr = va_arg(ap,uint32);
    uint8 icmp_type = va_arg(ap,uint32);
    uint8 icmp_code = va_arg(ap,uint32);
    return icmp_sockalloc(f, raddr, icmp_type, icmp_code);
  }

  else
    panic("unsupported protocal");
}


void
sockclose(struct sock *si)
{
  struct sock **pos;
  struct mbuf *m;

  // remove from list of sockets
  acquire(&lock);
  pos = &sockets;
  while (*pos) {
    if (*pos == si){
      *pos = si->next;
      break;
    }
    pos = &(*pos)->next;
  }
  release(&lock);

  // free any pending mbufs
  while (!mbufq_empty(&si->rxq)) {
    m = mbufq_pophead(&si->rxq);
    mbuffree(m);
  }

  kfree((char*)si);
}

int
sockread(struct sock *si, uint64 addr, int n)
{
  struct proc *pr = myproc();
  struct mbuf *m;
  int len;

  acquire(&si->lock);
  while (mbufq_empty(&si->rxq) && !pr->killed) {
    sleep(&si->rxq, &si->lock);
  }
  if (pr->killed) {
    release(&si->lock);
    return -1;
  }
  m = mbufq_pophead(&si->rxq);
  release(&si->lock);

  len = m->len;
  if (len > n)
    len = n;
  if (copyout(pr->pagetable, addr, m->head, len) == -1) {
    mbuffree(m);
    return -1;
  }
  mbuffree(m);
  return len;
}

int
sockwrite(struct sock *si, uint64 addr, int n)
{
  struct proc *pr = myproc();
  struct mbuf *m;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  if (copyin(pr->pagetable, mbufput(m, n), addr, n) == -1) {
    mbuffree(m);
    return -1;
  }
  if(si->ip_type == IPPROTO_UDP)
    net_tx_udp(m, si->raddr, si->lport, si->rport);
  else if(si->ip_type == IPPROTO_ICMP)
    net_tx_icmp(m, si->raddr, si->icmp_type, si->icmp_code);
  return n;
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  //
  struct sock *si;

  acquire(&lock);
  si = sockets;
  while (si) {
    if (si->ip_type == IPPROTO_UDP && si->raddr == raddr && si->lport == lport && si->rport == rport)
      goto found;
    si = si->next;
  }
  release(&lock);
  mbuffree(m);
  return;

found:
  acquire(&si->lock);
  mbufq_pushtail(&si->rxq, m);
  wakeup(&si->rxq);
  release(&si->lock);
  release(&lock);
}




void sockrecvicmp(struct mbuf* m, uint32 raddr, uint8 ttl) {
  struct sock *si;
  acquire(&lock);
  si = sockets;
  
  while(si) {
    // it's hard to dispatch with process id
    // because actually they are in data section
    // so only dispatch with ip addresss
    if (si->ip_type == IPPROTO_ICMP && si->raddr == raddr)
      goto found;
    si = si->next;
    si->icmp_recvttl = ttl;
  }

  release(&lock);
  mbuffree(m);
  return;
  
 found:
  acquire(&si->lock);
  mbufq_pushtail(&si->rxq, m);
  wakeup(&si->rxq);
  release(&si->lock);
  release(&lock);
}