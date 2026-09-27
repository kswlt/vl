/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#include "communicationNet.h"
#include <iostream>
#include <thread>
#include "utils.h"
#include "xtlogger.h"

#define  UDP_HEADER_OFFSET  20

namespace XinTan {


  CommnunicationNet::CommnunicationNet(boost::asio::io_service& ios, std::string & logtag):
      logtagname(logtag),
      socketTcp(ios),
      tcpEndpoint(boost::asio::ip::address_v4::from_string("192.168.0.101"),7787),
      resolver(ios),
      tcpRecvPacket(XByteArray(4096)),
      socketUdp(ios),
      //udpEndpoint(udp::v4(), 7687),
      udpRecvBuffer(XByteArray(100096)),
      udpRecvPacket(XByteArray(100096))
  {
      address = "";
      isConnected = false;
      isConnecting = false;
      isUdpOpened = false;
      for(int i=0; i < 3; i++)
      {
        data_buffer[i] = XByteArray(320*240*8*2+1024);
        data_isUsed[i] = 0;
        data_count[i] = 0;
        data_sn[i] = 0;
        data_size[i] = 0;
        discard_frameid[i] = 0;
      }
  }

  CommnunicationNet::~CommnunicationNet()
  {
      disconnect();
      //closeUdp();
       if(socketUdp.is_open())
       {
          boost::system::error_code error;
          socketUdp.close(error);
       }
  }

  void openTcpThread(CommnunicationNet * pComuntNet)
  {
        XTLOGINFOEXT(pComuntNet->logtagname, "");
        boost::system::error_code error;
        pComuntNet->socketTcp.connect(pComuntNet->tcpEndpoint, error); // 连接服务
        if(error)
        {
            XTLOGWRNEXT(pComuntNet->logtagname,"error");
        }else
            pComuntNet->isConnecting = false;
  }

  bool CommnunicationNet::connect()
  {
      const std::lock_guard<std::mutex> lock(opencloseLock);
      if(Utils::ipIsValid(address)==false)
          return false;

       if(isConnected || isConnecting)
           return true;
       boost::asio::ip::address_v4 ipaddress = boost::asio::ip::address_v4::from_string(address);

       tcpEndpoint.address(ipaddress);

       isConnecting = true;
       std::thread openthred(openTcpThread, this);

       for(int i=0;i<3;i++)
       {
           if(isConnecting)
           {
               std::this_thread::sleep_for(std::chrono::seconds(1));
               continue;
           }
           break;
       }

       if(isConnecting)
       {
           socketTcp.close();
           openthred.join();

           std::this_thread::sleep_for(std::chrono::seconds(1));
           isConnecting = false;
           return false;
       }else
       {
           isConnected = true;
           openthred.join();
           XTLOGWRN("tcp connected");
           return true;
       }

  }
  void CommnunicationNet::disconnect()
  {
      const std::lock_guard<std::mutex> lock(opencloseLock);
       if((isConnected == false) && (socketTcp.is_open()==false))
          return;

        boost::system::error_code error;
        socketTcp.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
        if (!error) {

            XTLOGWRN("error");
            socketTcp.close(error);
            std::cout << "tcp closed " <<  std::endl;
        }

        isConnected = false;

  }
  bool CommnunicationNet::openUdp(uint16_t port)
  {
    const std::lock_guard<std::mutex> lock(opencloseLockudp);
    
    XTLOGINFOEXT(logtagname, "");
    if((isUdpOpened)&&(socketUdp.is_open()))
        return true;

    try{
        socketUdp.open(boost::asio::ip::udp::v4());
        socketUdp.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address_v4::any(), port);

        socketUdp.bind(endpoint);

        //        socketUdp.set_option(boost::asio::ip::multicast::join_group(
        //                                 boost::asio::ip::address_v4::from_string("239.255.255.76")
        //                                 , boost::asio::ip::address_v4::from_string("0.0.0.0"))
        //                             );
        //if(UdpMultiCastOn)
            socketUdp.set_option(boost::asio::ip::multicast::join_group(boost::asio::ip::address_v4::from_string("239.255.255.76")) );

        isUdpOpened = true;

        XTLOGINFOEXT(logtagname, "udp opened");
		return true;

	}catch( std::exception &e)
	{
        std::cout << "openUdp:exception " << e.what() <<  std::endl;
        
        XTLOGINFOEXT(logtagname, "udp open exception");
	}
    return false;

  }
  
  void CommnunicationNet::closeUdp()
  {
      const std::lock_guard<std::mutex> lock(opencloseLockudp);
	  
      if((isUdpOpened == false)&&(socketUdp.is_open()==false))
         return;      
      isUdpOpened = false;
	 
	  XTLOGINFO("");

      boost::system::error_code error;
      try{
          socketUdp.cancel(error);
          socketUdp.close(error);
          std::cout  <<  std::endl << "udp closed " <<  std::endl;
	  }catch( std::exception &e)
	  {
          std::cout << "closeUdp:exception " << e.what() <<  std::endl;
          XTLOGWRN("exception");
      }
      if(error)
      {
          std::cout << "udp already closed " <<  std::endl;
          XTLOGWRN("udp already closed ");
      }
  }

  bool CommnunicationNet::transmitPackage(XByteArray data)
  {
      if(isConnected==false)
          return false;

        uint32_t data_len = (uint32_t) (data.size());
        std::ostringstream os;
        //data
        for (uint32_t i = 0; i < data_len; ++i) {
            os << static_cast<uint8_t>(data[i]);
        }

        boost::system::error_code error;

        socketTcp.write_some(boost::asio::buffer(os.str(), os.tellp()), error);
        if (error) {
            std::cout << "cmd wirte error" << std::endl;
            XTLOGWRN("cmd wirte error");
            return false;
        }
        return true;
  }

  bool CommnunicationNet::receivePackage(XByteArray & pkgData)
  {
        if(isConnected==false)
            return false;

        XByteArray buf(1500);
        boost::system::error_code error;

        uint32_t startmark = 0;
        uint32_t endmark = 0;
        uint32_t payloadlen =0;

        size_t len = 0;
        try{
            len = socketTcp.read_some(boost::asio::buffer(buf), error);
            if (error) {
                std::cout << "tcp rx error" << std::endl;
                disconnect();
                return false;
            }

        }catch( std::exception &e)
        {
            std::string logstr = "exception:read_some:exception ";
            logstr.append(e.what());
            std::cout << logstr <<  std::endl;
            XTLOGERR(logstr);
        }

        if(len<15)
            return false;

        buf.resize(len);
        startmark = Utils::getValueUint32Endian(&(buf[0]), Endian_Big);
        endmark = Utils::getValueUint32Endian(&(buf[len-4]), Endian_Big);
        if((startmark == 0x7EFFAA55) && (endmark == 0xFF7E55AA))
        {
            payloadlen = Utils::getValueUint32Endian(&(buf[4]), endianType);
            if(payloadlen != (buf.size()-12) )//确保数据尺寸正确
                return false;

            pkgData.assign(buf.begin()+8, buf.end()-4);//头去8，尾去4

            return true;
        }

        return false;
  }

  bool CommnunicationNet::udpReceiveFrame(XByteArray & pkgData)
  {
      if(isUdpOpened==false)
          return false;

       udp::endpoint remote_ep;
       boost::system::error_code ec;
       while(isUdpOpened){
           size_t len = socketUdp.receive_from(boost::asio::buffer(udpRecvBuffer), remote_ep, 0, ec);

           if(ec && ec!=boost::asio::error::message_size)
           {
               std::cout << "udp error" << len << std::endl;
               XTLOGWRN("udp error");
               return false;
           }
           udppackage_len = len;
           if(udppacket(udpRecvBuffer, pkgData))
           {
               return true;
           }
       }
       return false;
  }

  bool CommnunicationNet::udppacket(const XByteArray & p, XByteArray & pkgData)
  {
     uint32_t startpos = 0;
     bool repkg_ok = false;
     bool brepeat = false;

     do{

         uint16_t imagesn = Utils::getValueUint16Endian(&p[startpos+ 0], endianType);
         uint32_t totalsize = Utils::getValueUint32Endian(&p[startpos+2], endianType);
         uint16_t payloadSize = Utils::getValueUint16Endian(&p[startpos+6], endianType);
         uint32_t sentsize = Utils::getValueUint32Endian(&p[startpos+8], endianType);
         //uint32_t packetNum = Utils::getValueUint32Endian(&p[16], Endian_Big);
         bool istoframe = true;

         if((payloadSize > 1400)|| ((payloadSize+startpos) >udppackage_len))
         {
             uint8_t endian = Endian_Little;
             if(endianType == Endian_Little)
                 endian = Endian_Big;

             imagesn = Utils::getValueUint16Endian(&p[startpos+0], endian);
             totalsize = Utils::getValueUint32Endian(&p[startpos+2], endian);
             payloadSize = Utils::getValueUint16Endian(&p[startpos+6], endian);
             sentsize = Utils::getValueUint32Endian(&p[startpos+8], endian);

             if((payloadSize > 1400) || (sentsize > totalsize) || (totalsize > 1200000))
                break;

             endianType = endian;
         }else if(payloadSize < 200)
         {
             if(p[startpos+UDP_HEADER_OFFSET+8] == 252)
             {
                 uint32_t startmark = Utils::getValueUint32Endian(&p[startpos+UDP_HEADER_OFFSET], Endian_Big);
                 uint32_t endmark = Utils::getValueUint32Endian(&p[startpos+UDP_HEADER_OFFSET+payloadSize-4], Endian_Big);

                 if((startmark == 0x7EFFAA55) && (endmark == 0xFF7E55AA))
                 {
                     pkgData.assign(p.begin()+startpos+UDP_HEADER_OFFSET+8, p.begin() +startpos+UDP_HEADER_OFFSET+ payloadSize-4);

                     repkg_ok = true;
                     istoframe = false;
                     //break;
                     //return true;
                 }else
                 {
                     std::cout << "imu frame start " << std::hex << startmark << std::endl;
                     std::cout << "imu frame end " << std::hex << endmark << std::endl;
                 }
             }
         }

        for(uint32_t i=0; i < 3; i++)
        {
            if( discard_frameid[i] > 0)
            {
                int32_t  curr_id = imagesn;
                int32_t  discard_id = discard_frameid[i];

                if(discard_id == curr_id)
                    istoframe = false;

                if(abs(curr_id - discard_id) > 30)
                    discard_frameid[i] = 0;
            }
        }

      if(istoframe)
      {
         uint32_t bufIndex = 10;
         //找在用的index
         for(uint32_t i=0; i < 3; i++)
         {
             if(data_isUsed[i] == 1)
                 if(data_sn[i] == imagesn)
                     bufIndex = i;
         }

         if(bufIndex > 2)
         {
             //找沒用的index
             for(uint32_t i=0; i < 3; i++)
             {
                 if(data_isUsed[i] == 0)
                 {
                      bufIndex = i;
                      data_sn[bufIndex] = imagesn;
                      data_size[bufIndex] = totalsize;
                      data_count[bufIndex] = 0;
                      data_isUsed[i] = 1;
                 }
             }
         }

         if(bufIndex > 2)
         {
            //丟最早的index
            if(data_sn[0] < data_sn[1])
            {
                bufIndex = 0;
                if(data_sn[0] > data_sn[2])
                    bufIndex = 2;
            }
            else if(data_sn[1] < data_sn[2])
                bufIndex = 1;
            else
                bufIndex = 2;

            if(data_sn[bufIndex] < 5)//sn 翻转点处理
            {
                int validindex = 10;
                for(uint32_t i=0; i < 3; i++)
                {
                    if(bufIndex != i)
                    {
                        if(data_sn[i] > 65500)
                        {
                            validindex = i;
                            break;
                        }
                    }
                }
                if(validindex < 3)
                {
                    //XTLOGWRN("udp sn mirror0="+std::to_string(data_sn[validindex]));
                    for(uint32_t i=0; i < 3; i++)
                    {
                        if((bufIndex != i) && (validindex != i))
                        {
                            if(data_sn[i] > 65500)
                            {
                                if(data_sn[i] < data_sn[validindex])
                                    bufIndex = i;
                                else
                                    bufIndex = validindex;
                            }else
                                bufIndex = validindex;
                            XTLOGWRN("udp sn overturn="+std::to_string(data_sn[bufIndex]) +": " +std::to_string(data_sn[0]) +" " +std::to_string(data_sn[1])+" " +std::to_string(data_sn[2]));
                        }
                    }
                }
            }

            //记录丢弃id
            {
                uint16_t discard_index = 0;
                for(uint32_t i=0; i < 3; i++)
                {
                    if(discard_frameid[i] == 0)
                    {
                        discard_index = i;
                        break;
                    }
                }
                discard_frameid[discard_index] = data_sn[bufIndex];
                std::cout << "udp discard frame " << std::to_string(data_sn[bufIndex]) << " discardindex " << std::to_string(discard_index) <<std::endl;
                //XTLOGWRN("udp discard frame "+std::to_string(data_sn[bufIndex]));
            }

            data_isUsed[bufIndex] = 1;
            data_sn[bufIndex] = imagesn;
            data_size[bufIndex] = totalsize;
            data_count[bufIndex] = 0;
         }

         memcpy(&data_buffer[bufIndex][sentsize], &p[startpos+UDP_HEADER_OFFSET], payloadSize);
         data_count[bufIndex] += payloadSize;

         if(data_count[bufIndex] >= data_size[bufIndex])
         {
              data_isUsed[bufIndex] = 0;

              if(data_count[bufIndex] == totalsize)
              {
                  uint32_t startmark = Utils::getValueUint32Endian(&(data_buffer[bufIndex][0]), Endian_Big);
                  uint32_t endmark = Utils::getValueUint32Endian(&(data_buffer[bufIndex][totalsize-4]), Endian_Big);

                  if((startmark == 0x7EFFAA55) && (endmark == 0xFF7E55AA))
                  {
                      pkgData.assign(data_buffer[bufIndex].begin()+8, data_buffer[bufIndex].begin() + totalsize-4);
                      repkg_ok = true;
                      //break;
                      //return true;
                  }else
                    std::cout << "udp frame error " << std::to_string(data_sn[bufIndex]) <<std::endl;
              }
         }
       }
         startpos += payloadSize+20;
         if( startpos < udppackage_len)
         {
             brepeat = true;
         }else
             brepeat = false;

     }while(brepeat);

     udppackage_len = 0;
     if(repkg_ok)
         return true;
     else
         return false;
  }


} //end namespace XinTan
