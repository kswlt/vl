/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "frame.h"


namespace XinTan {


class Commnunication{

public:

    bool isConnected = false;
    bool isUdpOpened = false;
    bool isConnecting = false;
    uint8_t endianType = 1;//大端
    
    std::string address;    
    bool UdpMultiCastOn = false;

    std::mutex opencloseLock;
    std::mutex opencloseLockudp;
  
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool openUdp(uint16_t port =7687) = 0;
    virtual void closeUdp() = 0;
    virtual bool transmitPackage(XByteArray data) = 0;
 
    virtual bool receivePackage(XByteArray & pkgData) = 0;
    virtual bool udpReceiveFrame(XByteArray & pkgData) = 0;


};

} //end namespace xintan
