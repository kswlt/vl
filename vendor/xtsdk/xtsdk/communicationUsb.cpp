/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#include "communicationUsb.h"
#include <iostream>
#include <thread>
#include "utils.h"
#include "xtlogger.h"


namespace XinTan {

  CommnunicationUsb::CommnunicationUsb(boost::asio::io_service& ios, std::string & logtag):
      logtagname(logtag),
      serialPort(ios)
  {
        isConnected = false;
        address = "";
        isConnecting = false;

        payloadbuf.resize(1500000);
        remainbuf.resize(3000000);
  }

  CommnunicationUsb::~CommnunicationUsb()
  {
      disconnect();
  }

  bool CommnunicationUsb::openUdp(uint16_t port){
        return false;
  }

  void CommnunicationUsb::closeUdp()
  {

  }


  bool  CommnunicationUsb::udpReceiveFrame(XByteArray & pkgData)
  {
      return false;
  }

  void openUsbThread(CommnunicationUsb * pComuntUsb)
  {
        boost::system::error_code error;
		try{
			pComuntUsb->serialPort.open(pComuntUsb->address, error); // 连接服务
            std::cout << "usb opened " <<  std::endl;
			
		}catch( std::exception &e)
		{
			std::cout << "openTcpThread:exception " << e.what() <<  std::endl;
            XTLOGWRNEXT(pComuntUsb->logtagname,"openTcpThread:exception");

		}
        if(error)
        {
        }else
        {

#ifdef WIN32
            pComuntUsb->serialPort.set_option(boost::asio::serial_port_base::baud_rate(10000000));
            pComuntUsb->serialPort.set_option(boost::asio::serial_port_base::character_size(8));
            pComuntUsb->serialPort.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
            pComuntUsb->serialPort.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
            pComuntUsb->serialPort.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
#endif
            pComuntUsb->isConnecting = false;
        }
  }

  bool CommnunicationUsb::connect()
  {
        const std::lock_guard<std::mutex> lock(opencloseLock);
        if(Utils::isComport(address)==false)
            return false;

        if(isConnected)
            return true;

        isConnecting = true;

        std::thread openthred(openUsbThread, this);


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
           openthred.join();
           return false;
        }else
        {
           isConnected = true;
           openthred.join();
           //PurgeComm(serialPort.native_handle(),PURGE_RXCLEAR|PURGE_RXABORT);
           return true;
        }

  }
  void CommnunicationUsb::disconnect()
  {      
       const std::lock_guard<std::mutex> lock(opencloseLock);
       if(isConnected == false)
          return;

        boost::system::error_code error;
		try{
            serialPort.cancel(error);
            serialPort.close( error); // 关闭串口
            std::cout << "usb closed " <<  std::endl;
		}catch( std::exception &e)
		{
			std::cout << "Usb disconnect:exception " << e.what() <<  std::endl;
            XTLOGWRN("Usb disconnect:exception ");
		}
       if(error)
       {
           std::cout << "usb already closed " <<  std::endl;
           XTLOGWRN("usb already closed ");
       }

       isConnected = false;
  }


  bool CommnunicationUsb::transmitPackage(XByteArray data)
  {
        if(isConnected==false)
            return false;

        uint32_t data_len = (uint32_t) (data.size());

        std::ostringstream os;
        for (uint32_t i = 0; i < data_len; ++i) {
            os << static_cast<uint8_t>(data[i]);
        }

        boost::system::error_code error;
        boost::asio::write(serialPort,boost::asio::buffer(os.str(), os.tellp()), error);
        if (error) {
            std::cout << "cmd wirte error" << std::endl;
            XTLOGWRN("cmd wirte error ");
            return false;
        }
        return true;
  }


  bool CommnunicationUsb::receivePackage(XByteArray & pkgData)
  {
      if(isConnected == false)
          return false;

      boost::system::error_code error;
      XByteArray buf_head(10);

      uint32_t startmark = 0;
      uint32_t endmark = 0;
      uint32_t payloadlen =0;

      //读包头
      size_t  headlen =0;
      size_t  datalen =0;
      try{
          headlen = boost::asio::read(serialPort, boost::asio::buffer(buf_head), boost::asio::transfer_exactly(8), error);
      }catch( std::exception &e)
      {
          std::cout << "Usb read head:exception " << e.what() <<  std::endl;
          XTLOGWRN("Usb read head:exception");
      }

      if(error)
      {
          std::cout << "usb head error" << std::endl;
          XTLOGERR("usb head error");
          disconnect();
          return false;
      }

      //读取到包尾
      startmark = Utils::getValueUint32Endian(&(buf_head[0]), Endian_Big);
      if(startmark == 0x7EFFAA55)
      {
          payloadlen = Utils::getValueUint32Endian(&(buf_head[4]),  endianType);
          if(payloadlen > 1200000)
             payloadlen = ((payloadlen >> 24) & 0xff) |  ((payloadlen >> 8) & 0xff00) |   ((payloadlen << 8) & 0xff0000) |   (payloadlen << 24);

          if((payloadlen > 2) && (payloadlen < 1200000))
          {
              //按预测读取到包尾
              try{
                  datalen = boost::asio::read(serialPort, boost::asio::buffer(payloadbuf), boost::asio::transfer_exactly(payloadlen+4), error);
              }catch( std::exception &e)
              {
                  std::cout << "Usb read playload:exception " << e.what() <<  std::endl;
                  XTLOGWRN("Usb read data:exception");
              }
              if(error)
              {
                  std::cout << "usb read playload error" << std::endl;
                  XTLOGWRN("usb read playload error");
                  disconnect();
                  return false;
              }

              endmark = Utils::getValueUint32Endian(&(payloadbuf[datalen-4]), Endian_Big);
              if(endmark == 0xFF7E55AA)
              {
                  if((datalen > 6) && (payloadlen == (datalen - 4)))
                  {
                      pkgData.assign(payloadbuf.begin(), payloadbuf.begin() + datalen -4);//头去8，尾去4
                      return true;
                  }

                  std::cout << "usb rx pkg size error " << std::to_string(datalen) << std::endl;
              }else
              {
                  //std::cout << "usb endmark error " << std::to_string(datalen) << std::endl;
              }

          }else
          {
              std::cout << " usb  playload size error " +std::to_string(payloadlen) << std::endl;
          }
      }else
      {
          payloadbuf[1] = buf_head[1];
          payloadbuf[2] = buf_head[2];
          payloadbuf[3] = buf_head[3];
          payloadbuf[4] = buf_head[4];
          payloadbuf[5] = buf_head[5];
          payloadbuf[6] = buf_head[6];
          payloadbuf[7] = buf_head[7];
          datalen = 7;
      }

      //读取遗留
      size_t remainlen = serialPort.read_some(boost::asio::buffer(remainbuf), error);
      if(error)
      {
          std::cout << "usb remain read error " << std::endl;
          disconnect();
          return false;
      }

//        if((datalen > 0) && (remainlen >0) && (endianType == Endian_Little))//对剩余数据进行分析，提取正确的包
//        {
//            //合并接收到的数据
//            mergebuf.resize(datalen);
//            mergebuf.assign(payloadbuf.begin(), payloadbuf.begin()+ datalen);
//            mergebuf.insert(mergebuf.end(), remainbuf.begin(), remainbuf.begin() + remainlen);
//            size_t mergesize = mergebuf.size();
//            //std::cout << "usb parse remain data  " <<  std::to_string(mergesize) << std::endl;

//            int datastartpos = 0;
//            int dataendpos = 0;
//            uint32_t repayloadlen = 0;

//            for(int i=0; i < (mergesize-12); i++)
//            {
//                uint32_t restartmark = (mergebuf[i+ 3] << 24) | (mergebuf[i+ 2] << 16) | (mergebuf[i+ 1] << 8) | mergebuf[i];
//                if(restartmark == 0x55AAFF7E) //0x7EFFAA55 找到开头
//                {
//                    i = i + 4;
//                    repayloadlen = (mergebuf[i+ 3] << 24) | (mergebuf[i+ 2] << 16) | (mergebuf[i+ 1] << 8) | mergebuf[i];
//                    datastartpos = i+4;
//                    dataendpos = datastartpos + repayloadlen;

//                    if((repayloadlen > 2) && (repayloadlen < 700000) && (dataendpos < (mergesize - 3)))
//                    {
//                        uint32_t reendmark = (mergebuf[dataendpos + 3] << 24) | (mergebuf[dataendpos+ 2] << 16) | (mergebuf[dataendpos+ 1] << 8) | mergebuf[dataendpos];

//                        //std::cout << " reendmark  " << std::hex << reendmark  << std::endl;
//                        if(reendmark == 0xAA557EFF )//0xFF7E55AA
//                        {
//                            std::cout << " find pkg:  " <<  std::to_string(repayloadlen) << std::endl;
//                            pkgData.assign(mergebuf.begin() + datastartpos, mergebuf.begin() + datastartpos + repayloadlen);//头去8，尾去4
//                            return true;
//                        }
//                    }
//                }
//            }
//        }

      std::cout << " usb lost data:  " <<  std::to_string(remainlen + datalen) << std::endl;
      return false;
    }

} //end namespace XinTan
