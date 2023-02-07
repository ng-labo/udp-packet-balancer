## udp-packet-balancer

simplest udp packet proxy to load balancing.

motivation

: desire a program like a nginx udp balancer, with spoofing source address.

target situaiton

: udp client-server communication which use a bit long alive session.

: the case it is easy to add server process in different port.

### udp packet proxy to balance load

n udp-clients access to one udp-balancer, it forwards packets to m udp-server.
and send back packets from udp-server to each client.

```
client 1 <->  |              | <-> | udp-server-process 1 |
client 2 <->  | udp-balancer | <-> | udp-server-process 2 |
   :          |              |       
client n <->  |              | <-> | udp-server-process m |
```

###

```
$ autoreconf --verbose --install --force
$ ./configure
$ make
```

### TODO
- spoofing source address in udp-balancer(source nat)
- to be selective some balancing algorithm
- ipv6 available(client side when not nat)
- the way to dump some counters
