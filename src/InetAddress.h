#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>

class InetAddress
{
public:
    explicit InetAddress(uint16_t port, std::string ip = "127.0.0.1");
    explicit InetAddress(const struct sockaddr_in &addr) : addr_(addr) { };
    
    std::string toIP() const;
    uint16_t toPort() const;
    std::string toIPPort() const;

    const sockaddr_in *getSockaddr() const { return &addr_; }
    void setSockAddr(const struct sockaddr_in &addr) { addr_ = addr; }

private:
    struct sockaddr_in addr_;
};
