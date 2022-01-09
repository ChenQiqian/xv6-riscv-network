#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/net.h"
#include "user/user.h"

// Encode a DNS name
static void
encode_qname(char *qn, char *host)
{
  char *l = host; 

  for(char *c = host; c < host+strlen(host)+1; c++) {
    if(*c == '.') {
      *qn++ = (char) (c-l);
      for(char *d = l; d < c; d++) {
        *qn++ = *d;
      }
      l = c+1; // skip .
    }
  }
  *qn = '\0';
}

// Decode a DNS name
static void
decode_qname(char *qn)
{
  while(*qn != '\0') {
    int l = *qn;
    if(l == 0)
      break;
    for(int i = 0; i < l; i++) {
      *qn = *(qn+1);
      qn++;
    }
    *qn++ = '.';
  }
}


uint32 ip2int(char *sIP)
{
    int i = 0;
    uint32 v1 = 0, v2 = 0, v3 = 0, v4 = 0;
    for (i = 0; sIP[i] != '\0'; ++i)
        ;
    for (i = 0; sIP[i] != '.'; ++i)
        v1 = v1 * 10 + sIP[i] - '0';
    for (++i; sIP[i] != '.'; ++i)
        v2 = v2 * 10 + sIP[i] - '0';
    for (++i; sIP[i] != '.'; ++i)
        v3 = v3 * 10 + sIP[i] - '0';
    for (++i; sIP[i] != '\0'; ++i)
        v4 = v4 * 10 + sIP[i] - '0';
    return (v1 << 24) + (v2 << 16) + (v3 << 8) + v4;
}


// Make a DNS request
static int
dns_req(uint8 *obuf, char *s)
{
  int len = 0;

  struct dns *hdr = (struct dns *) obuf;
  hdr->id = htons(6828);
  hdr->rd = 1;
  hdr->qdcount = htons(1);

  len += sizeof(struct dns);

  // qname part of question
  char *qname = (char *) (obuf + sizeof(struct dns));
  encode_qname(qname, s);
  len += strlen(qname) + 1;

  // constants part of question
  struct dns_question *h = (struct dns_question *) (qname+strlen(qname)+1);
  h->qtype = htons(0x1);
  h->qclass = htons(0x1);

  len += sizeof(struct dns_question);
  return len;
}

// Process DNS response
static uint32
dns_rep(uint8 *ibuf, int cc, uint32 q)
{
  struct dns *hdr = (struct dns *) ibuf;
  int len;
  char *qname = 0;
  int record = 0;
  uint32 ip_receive;

  if(!hdr->qr) {
    fprintf(2, "Not a DNS response for %d\n", ntohs(hdr->id));
    exit(1);
  }

  if(hdr->id != htons(6828))
    fprintf(2, "DNS wrong id: %d\n", ntohs(hdr->id));

  if(hdr->rcode != 0) {
    fprintf(2, "DNS rcode error: %x\n", hdr->rcode);
    exit(1);
  }

  //printf("qdcount: %x\n", ntohs(hdr->qdcount));
  //printf("ancount: %x\n", ntohs(hdr->ancount));
  //printf("nscount: %x\n", ntohs(hdr->nscount));
  //printf("arcount: %x\n", ntohs(hdr->arcount));

  len = sizeof(struct dns);

  for(int i =0; i < ntohs(hdr->qdcount); i++) {
    char *qn = (char *) (ibuf+len);
    qname = qn;
    decode_qname(qn);
    len += strlen(qn)+1;
    len += sizeof(struct dns_question);
  }

  for(int i = 0; i < ntohs(hdr->ancount); i++) {
    char *qn = (char *) (ibuf+len);

    if((int) qn[0] > 63) {  // compression?
      qn = (char *)(ibuf+qn[1]);
      len += 2;
    } else {
      decode_qname(qn);
      len += strlen(qn)+1;
    }

    struct dns_data *d = (struct dns_data *) (ibuf+len);
    len += sizeof(struct dns_data);
    if(!q)
        printf("type %d ttl %d len %d:\n", ntohs(d->type), ntohl(d->ttl), ntohs(d->len));
    if(ntohs(d->type) == ARECORD && ntohs(d->len) == 4) {
      record = 1;
      if(!q)
        fprintf(1, "  DNS arecord for %s is ", qname ? qname : "" );
      uint8 *ip = (ibuf+len);
      if(!q)
        printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      ip_receive = (ip[0] << 24) + (ip[1] << 16) + (ip[2] << 8) + ip[3];
      len += ntohs(d->len);
    }
    else if(ntohs(d->type) == CNAME) {
      if(!q)
        fprintf(1, "  DNS cname record.");
      len += ntohs(d->len);
    }
  }

  if(len != cc) {
    fprintf(2,"Processed %d data bytes but received %d\n", len, cc);
    exit(1);
  }
  if(!record) {
    fprintf(2,"Didn't receive an arecord\n");
    exit(1);
  }
  return ip_receive;
}



uint32
dns(char *ss,uint32 q)
{
  // add a point on the 
  char s[100];
  memmove(s,ss,strlen(ss));
  s[strlen(s)+1] = '\0';
  s[strlen(s)] = '.';
  
  #define N 1000
  uint8 obuf[N];
  uint8 ibuf[N];
  uint32 dst;
  int fd;
  int len;

  memset(obuf, 0, N);
  memset(ibuf, 0, N);

  // 8.8.8.8: google's name server
  dst = (8 << 24) | (8 << 16) | (8 << 8) | (8 << 0);

  if((fd = connect(dst, 10000, 53)) < 0){
    fprintf(2, "dns: connect() failed\n");
    exit(1);
  }

  len = dns_req(obuf,s);

  if(write(fd, obuf, len) < 0){
    fprintf(2, "dns: send() failed\n");
    exit(1);
  }
  int cc = read(fd, ibuf, sizeof(ibuf));
  if(cc < 0){
    fprintf(2, "dns: recv() failed\n");
    exit(1);
  }
  uint32 ans = dns_rep(ibuf, cc, q);

  close(fd);
  return ans;
}  