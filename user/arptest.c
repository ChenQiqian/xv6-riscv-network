#include "kernel/types.h"
#include "user/user.h"

int main(int argc,char** argv) {
  if(argc == 1)
  {
    fprintf(1,"Usage: arptest target_name \n");
    exit(0);
  }
  
  if(connect_arp(ip2int(argv[1])) < 0) 
  {
    fprintf(1, "ARP for IP: %s Failed.\n", argv[1]);
  }
  exit(0);
}