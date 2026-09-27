/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include "communication.h"

#include <boost/asio.hpp>
#include <boost/signals2.hpp>

using boost::asio::serial_port;

namespace XinTan {

class CommnunicationUsb: public Commnunication{

public:
    CommnunicationUsb(boost::asio::io_service& ios, std::string & logtag);
    ~CommnunicationUsb();
    std::string & logtagname;

    bool connect();
    void disconnect();
    bool openUdp(uint16_t port =7687);
    void closeUdp();
    bool transmitPackage(XByteArray data);

    bool receivePackage(XByteArray & pkgData);
    bool udpReceiveFrame(XByteArray & pkgData);

    serial_port  serialPort;
    XByteArray payloadbuf;
    XByteArray remainbuf;

};

} //end namespace xintan


