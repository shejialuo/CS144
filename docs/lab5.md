# Lab 5

First you should carefully read the docs provided by lab. I give some important things here.

> If the destination Ethernet address is unknown, broadcast an ARP request for the next hop's Ethernet address and queue the IP datagram so it can be sent after the ARP reply is received. If the network already interface already sent an ARP request about the same IP address in the last five seconds, don't send a second request.

For the above description, it is obvious that we need to stores the IP datagram and also the time since it has been sent. When we receive a new IP datagram we need to check whether it is in the list. I use the following data structure:

```c++
    //! information for the block ethernet frame and the time since it has been sent
    struct BlockedEthernetFrame {
        EthernetFrame _frame{};
        size_t _time{};
    };

    //! mapping from next-hop-ip to BlockedEthernetFrame
    std::unordered_map<uint32_t, BlockedEthernetFrame> blocked{};
```

> If the inbound frame is ARP, parse the payload as an `ARPMessage` and, if successful, remember the mapping between the sender's IP address and Ethernet address for 30 seconds

As you can see, we need to maintain the mapping cache from ip address to the Ethernet address and with expiration time 30 seconds. I use the following data structure.

```c++
    //! information for the Ethernet cache
    struct EthernetEntry {
        EthernetAddress _mac{};
        size_t _time{};
    };

    //! the mapping cache from next-hop-ip to EthernetAddress
    std::unordered_map<uint32_t, EthernetEntry> _arp_cache{};
```

## Auxiliary functions

For this lab, we need to create `EthernetFrame` with different type (`TYPE_IPv4` and `TYPE_ARP`) and with different payload. For `TYPE_IPv4`, we do not consider payload, for `TYPE_ARP`, we need to consider its payload, we need to create the `ARPMessage` class.

So I first define a function named `new_ethernet_frame`, which unifies the process of creating different kinds of `EthernetFrame`.

```c++
EthernetFrame NetworkInterface::new_ethernet_frame(uint16_t type,
                                                   EthernetAddress src,
                                                   EthernetAddress dst,
                                                   BufferList payload) {
    EthernetFrame frame{};
    frame.header().type = type;
    frame.header().src = src;
    frame.header().dst = dst;
    frame.payload() = payload;

    return frame;
}

void NetworkInterface::set_ethernet_frame_dst(EthernetFrame &frame, EthernetAddress dst) { frame.header().dst = dst; }
```

Then, i define a function named `create_arp_message`, which creates `ARPMessage` instance.

```c++
ARPMessage NetworkInterface::create_arp_message(uint32_t sender_ip_address,
                                                EthernetAddress sender_ethernet_address,
                                                uint32_t target_ip_address,
                                                EthernetAddress target_ethernet_address,
                                                uint16_t opcode) {
    ARPMessage message{};
    message.sender_ip_address = sender_ip_address;
    message.sender_ethernet_address = sender_ethernet_address;
    message.target_ip_address = target_ip_address;
    message.target_ethernet_address = target_ethernet_address;
    message.opcode = opcode;
    return message;
}
```

## Code

This lab is not difficult, you could see my code for explanation, there are a lot of comments which describe the intent of the code.

### send_datagram

```c++
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // create a ipv4 type EthernetFrame
    EthernetFrame ipv4_ethernet_frame =
        new_ethernet_frame(EthernetHeader::TYPE_IPv4, _ethernet_address, {}, dgram.serialize());

    // When the hardware address is in cache and it does not expire.
    if (_arp_cache.find(next_hop_ip) != _arp_cache.end() && _arp_cache[next_hop_ip]._time <= 30000) {
        set_ethernet_frame_dst(ipv4_ethernet_frame, _arp_cache[next_hop_ip]._mac);
        frames_out().push(ipv4_ethernet_frame);
    } else {
        // We do not allow ARP flood, add a rate limit
        if (blocked.find(next_hop_ip) != blocked.end() && blocked[next_hop_ip]._time <= 5000)
            return;

        ARPMessage arp_message = create_arp_message(
            _ip_address.ipv4_numeric(), _ethernet_address, next_hop_ip, {}, ARPMessage::OPCODE_REQUEST);
        EthernetFrame arp_ethernet_frame = new_ethernet_frame(EthernetHeader::TYPE_ARP,
                                                              _ethernet_address,
                                                              ETHERNET_BROADCAST,
                                                              BufferList{std::move(arp_message.serialize())});

        BlockedEthernetFrame new_blocked_datagram{};
        new_blocked_datagram._time = 0;
        new_blocked_datagram._frame = ipv4_ethernet_frame;

        blocked.insert({next_hop_ip, new_blocked_datagram});
        frames_out().push(arp_ethernet_frame);
    }
}
```

### recv_frame

```c++
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    optional<InternetDatagram> new_datagram{};

    // We need to check whether the EthernetFrame is correct
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return new_datagram;
    }

    // If the EthernetFrame type is IPv4, we just parse its payload
    // to get the ip segment
    if (frame.header().type == frame.header().TYPE_IPv4) {
        InternetDatagram ipv4_datagram{};
        ipv4_datagram.parse(frame.payload());
        new_datagram.emplace(ipv4_datagram);
    } else {
        ARPMessage arp_message{};
        arp_message.parse(frame.payload());

        // If it asks for mac
        if (arp_message.target_ip_address != _ip_address.ipv4_numeric()) {
            return new_datagram;
        }

        // As long as we receive a ARPMessage, we should update the cache and
        // pushes the ipv4 segment.
        _arp_cache[arp_message.sender_ip_address]._mac = arp_message.sender_ethernet_address;
        _arp_cache[arp_message.sender_ip_address]._time = 0;
        auto iter = blocked.find(arp_message.sender_ip_address);
        if (iter != blocked.end()) {
            iter->second._frame.header().dst = arp_message.sender_ethernet_address;
            frames_out().push(iter->second._frame);
            blocked.erase(iter);
        }

        // When sending
        if (arp_message.opcode == arp_message.OPCODE_REQUEST) {
            ARPMessage message = create_arp_message(_ip_address.ipv4_numeric(),
                                                    _ethernet_address,
                                                    arp_message.sender_ip_address,
                                                    arp_message.sender_ethernet_address,
                                                    ARPMessage::OPCODE_REPLY);

            EthernetFrame arp_ethernet_frame = new_ethernet_frame(EthernetHeader::TYPE_ARP,
                                                                  _ethernet_address,
                                                                  arp_message.sender_ethernet_address,
                                                                  BufferList{std::move(message.serialize())});
            frames_out().push(arp_ethernet_frame);
        }
    }
    return new_datagram;
}
```

### tick

```c++
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // We should iterate the `blocked`
    for (auto &&entry : blocked) {
        entry.second._time += ms_since_last_tick;
    }
    // We should iterate the `_arp_cache`
    for (auto &&cache : _arp_cache) {
        cache.second._time += ms_since_last_tick;
    }
}
```
