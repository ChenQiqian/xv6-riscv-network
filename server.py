import socket
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
addr = ('localhost', int(sys.argv[1]))
print('listening on %s port %s' % addr, file=sys.stderr)
sock.bind(addr)

while True:
    buf, raddr = sock.recvfrom(4096)
    s = buf.decode("utf-8")
    print(s, file=sys.stderr)
    
    if s:
        sent = sock.sendto(s.encode('utf-8'), raddr)
