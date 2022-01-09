#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user.h"

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




int main(int argc, char **argv)
{
    // if (ping(argv[1]) < 0)
    // {
    //     fprintf(1, "ifconfig command failed");
    // }
    if (argc == 1)
    {
        fprintf(1, "Usage: ping target_name \n");
        return 0;
    }
    int fd;
    uint32 dst = ip2int(argv[1]);
    int id = getpid();
    int icmp_seq = 0, attempts = 4, send_ok = 4, recv_ok = 4;
    uint32 start_time = 0, end_time = 0;


    if ((fd = connect_icmp(dst, 8, 0)) < 0)
    {
        fprintf(2, "ping: connect() failed\n");
        return -1;
    }

    char ibuf[128];
    memset(&ibuf, 0, sizeof(ibuf));
    fprintf(1, "Ping %s ...\n", argv[1]);
    for (int i = 0; i < attempts; i++)
    {
        sleep(50);
        uint16 header[3];
        header[0] = 0x0800; // icmp type
        header[1] = id; // icmp code
        header[2] = icmp_seq;
        start_time = uptime();
        if (write(fd, header, sizeof(header)) < 0)
        {
            fprintf(2, "ping: send() failed\n");
            send_ok -= 1;
            continue;
        }

        int cc = read(fd, ibuf, sizeof(ibuf)-1);
        end_time = uptime();
        icmp_seq += 1;
        fprintf(1, "From %s: icmp_seq=%d  ttl=xxx  time=%dms\n", argv[1], icmp_seq, (end_time - start_time) * 10);
        if (cc < 0)
        {
            fprintf(2, "ping: recv() failed\n");
            recv_ok -= 1;
            continue;
        }
    }
    close(fd);
    fprintf(1, "%s ping statics:\n", argv[1]);
    fprintf(1, "%d packets transmittedd, %d received, %d% packet loss\n", send_ok, recv_ok, (4 - recv_ok) * 100 / send_ok);
    exit(0);
}