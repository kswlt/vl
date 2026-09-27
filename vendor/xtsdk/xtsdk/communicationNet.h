/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include "communication.h"
#include <boost/asio.hpp>
#include <boost/signals2.hpp>

using boost::asio::ip::address;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace XinTan {

class CommnunicationNet: public Commnunication{

public:
    CommnunicationNet(boost::asio::io_service& ios, std::string & logtag);
    ~CommnunicationNet();
    std::string & logtagname;

    bool connect();
    void disconnect();
    bool openUdp(uint16_t port =7687);
    void closeUdp();
    bool transmitPackage(XByteArray data);

    bool receivePackage(XByteArray & pkgData);
    bool udpReceiveFrame(XByteArray & pkgData);

    tcp::socket socketTcp;
    tcp::endpoint tcpEndpoint;

private:
    tcp::resolver resolver;
    XByteArray tcpRecvPacket;

    udp::socket socketUdp;
    XByteArray udpRecvBuffer;
    XByteArray udpRecvPacket;

    uint32_t  udppackage_len;


    std::shared_ptr<Frame> currentFrame;

    XByteArray data_buffer[3];
    uint32_t data_isUsed[3];
    uint32_t data_count[3];
    uint16_t data_sn[3];
    uint32_t data_size[3];


    uint16_t discard_frameid[3];

    bool udppacket(const XByteArray & p, XByteArray & pkgData);

};

} //end namespace xintan

