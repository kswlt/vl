
#include "../xtsdk/xtsdk.h"
#include "../xtsdk/utils.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <para_example.h>
#include <atomic>
#include <csignal>
#include <sys/stat.h>

using namespace XinTan;

XtSdk::Ptr xtsdk;
std::atomic<bool> keepRunning(true);
para_example para_set;
int is_set_config = 1;
double fwversionf = 0.0;
std::string bin_path = "";

void onFwUpdateEvent(const std::shared_ptr<CBEventData> &event)
{
    if (event->cmdstate != XinTan::CmdResp_OK)
    {
        if (event->data.size() == 2)
        {
            uint16_t sn = Utils::getValueUint16Endian(event->data.data(), xtsdk->getEndianType());
            std::cout << "fwupdate: sn= " << std::to_string(sn) << std::endl;
        }
    }

    // if(event->cmdstate != XinTan::CmdResp_REPORT)
    //     return;
    // if(event->data.size()==0)
    //     return;

    if (event->data[0] == 0x03)
    {
        if (event->data[1] == 0) // 升级成功
        {
            std::cout << "update success" << std::endl;
        }
        else
        {
            std::string progressbarResult = "";
            if (event->data[1] == 1) // 设备收到的数据尺寸和想要的尺寸不一致
                progressbarResult = "升级失败:size";
            else if (event->data[1] == 2) // flash 擦写错误
                progressbarResult = "升级失败:flash 擦写错误";
            else if (event->data[1] == 3) // 设备收到的固件数据校验失败
                progressbarResult = "升级失败:固件数据校验失败";
            else if (event->data[1] == 4) // 失败:timeout
                progressbarResult = "升级失败: 数据迁移失败";

            std::cout << progressbarResult << std::endl;
        }

        xtsdk->resetDev();
        keepRunning = false;
        // std::this_thread::sleep_for(std::chrono::seconds(3));
        // xtsdk->stop();
        // xtsdk->setCallback();
        // xtsdk->shutdown();

        // exit(0);
    }
}
std::string extractVersionFromPath(const std::string &binPath)
{
    // 找到最后一个斜杠的位置
    size_t lastSlashPos = binPath.find_last_of("/\\");

    // 如果没有找到斜杠，直接使用整个字符串作为文件名
    std::string fileName;
    if (lastSlashPos == std::string::npos)
    {
        fileName = binPath;
    }
    else
    {
        fileName = binPath.substr(lastSlashPos + 1);
    }

    // 找到最后一个下划线和点的位置
    size_t lastUnderscorePos = fileName.find_last_of('_');
    size_t dotPos = fileName.find_last_of('.');

    // 检查下划线和点是否存在
    if (lastUnderscorePos == std::string::npos || dotPos == std::string::npos)
    {
        return ""; // 如果未找到下划线或点，返回空字符串
    }

    // 检查下划线是否在点之前
    if (lastUnderscorePos < dotPos)
    {
        return fileName.substr(lastUnderscorePos + 1, dotPos - lastUnderscorePos - 1);
    }
    else
    {
        return ""; // 如果下划线不在点之前，返回空字符串
    }
}

// 检查设备的固件版本与文件中的版本是否匹配
bool checkFirmwareVersion(const double &fwversionf, const std::string &binPath)
{

    std::cout << "Current firmware version: " << fwversionf << std::endl;

    // 提取文件路径中的版本号
    std::string binVersion = extractVersionFromPath(binPath);
    double binVersionF = std::stod(binVersion);
    std::cout << "update fw version: " << binVersion << std::endl;
    // 比较版本号
    return (fwversionf == binVersionF);
}
void eventCallback(const std::shared_ptr<CBEventData> &event)
{
    std::cout << "event: " + event->eventstr + " " + std::to_string(event->cmdid) << std::endl;

    if (event->eventstr == "sdkState")
    {
        if (xtsdk->isconnect() && (event->cmdid == 0xfe)) // 端口打开后第一次连接上设备
        // if (xtsdk->isconnect())
        {
            xtsdk->stop();
            RespDevInfo devinfo;
            xtsdk->getDevInfo(devinfo);
            transform(devinfo.sn.begin(), devinfo.sn.end(), devinfo.sn.begin(), ::tolower);
            transform(devinfo.fwVersion.begin(), devinfo.fwVersion.end(), devinfo.fwVersion.begin(), ::tolower);

            std::cout << std::endl
                      << devinfo.fwVersion << std::endl;
            std::cout << devinfo.sn.c_str() << devinfo.chipidStr << std::endl;

            XTAPPLOG("DEV SN=" + devinfo.sn);

            int fwlen = devinfo.fwVersion.length();
            int vpos = devinfo.fwVersion.find('v');

            std::string verfstr = devinfo.fwVersion.substr(vpos + 1, fwlen - 2);
            double fwversionf = atof(verfstr.c_str());
            std::cout << "fw release version=" << fwversionf << "  str=" << verfstr << std::endl;
        }
        std::cout << "sdkstate= " + xtsdk->getStateStr() << std::endl;
    }
    else if (event->eventstr == "devState")
    {
        std::cout << "devstate= " + xtsdk->getStateStr() << std::endl;
    }
    else
    {
        if (event->cmdid == XinTan::REPORT_LOG) // log
        {
            std::string logdata;
            logdata.assign(event->data.begin(), event->data.end());
            std::cout << "log: " << logdata << std::endl;
        }
        if (event->cmdid == 50)
            onFwUpdateEvent(event);

        std::cout << "event: " + event->eventstr + " cmd=" << std::to_string(event->cmdid) << std::endl;
    }
}

void imgCallback(const std::shared_ptr<Frame> &imgframe)
{
    std::cout << "img: " + std::to_string((int)(imgframe->frame_id)) << " size: " + std::to_string((int)(imgframe->points.size())) << std::endl;

    // imgframe->points: 点云数据 ,
    // imgframe->distData : 深度数据， uint16_t类型，尺寸 imgframe->height*imgframe->width
    // imgframe->amplData : 信号幅度数据或者灰度数据， uint16_t类型, 尺寸 imgframe->height*imgframe->width
    // imgframe->distData : 深度数据， uint16_t类型，尺寸 imgframe->height*imgframe->width
}

void signalHandler(int signum)
{
    std::cout << "Received signal " << signum << ", exiting ..." << std::endl;
    keepRunning = false;
}

std::string SearchComPort(const std::string &cfgcomstr)
{
    std::string comport = "";
    boost::asio::io_service io;

    // Windows typically supports COM1 to COM256
    for (int i = 1; i <= 256; ++i)
    {
        std::string port_name = "COM" + std::to_string(i);

        try
        {
            boost::asio::serial_port serial(io, port_name);
            if (cfgcomstr == "")
            {
                return port_name;
            }

            if (port_name.compare(cfgcomstr) == 0)
            {
                return port_name;
            }
        }
        catch (boost::system::system_error &e)
        {
            // Ignore ports that cannot be opened
        }
    }
    return comport;
}
#ifdef _WIN32
void checkConnection(const std::string &usbport)
{
    while (keepRunning)
    {
        if (xtsdk->isconnect() == false)
        {
            std::string comport = SearchComPort("");
            std::cout << "Found COM port: " << comport << std::endl;
            if (usbport != comport)
            {
                xtsdk->setConnectIpaddress(comport);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
#else
void checkConnection(const std::string &usbport)
{
    while (keepRunning)
    {
        if (xtsdk->isconnect() == false)
        {
            if (chmod(usbport.c_str(), 0777) == 0)
            {
                std::cout << "Permissions changed to 777 for " << usbport << std::endl;
            }
            else
            {
                std::cerr << "Failed to change permissions for " << usbport << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
#endif
int main(int argc, char *argv[])
{
    xtsdk = std::make_shared<XtSdk>();

    std::string addresstring = "";
    if (argc <= 2)
    {
        std::cout << "Invalid param!" << std::endl;
        return 0;
    }

    addresstring = argv[1];
    bin_path = argv[2];

    if (Utils::ipIsValid(addresstring) || Utils::isComport(addresstring))
    {
        xtsdk->setConnectIpaddress(addresstring);
    }
    else
    {
        std::cout << "Invalid address or serialport!" << std::endl;
        return 0;
    }

    // xtsdk->setConnectIpaddress("192.168.2.101");
    // xtsdk->setConnectSerialportName("COM12"); //windows
    // xtsdk->setConnectSerialportName("/dev/ttyACM0"); //linux

    xtsdk->setSdkCloudCoordType((ClOUDCOORD_TYPE)para_set.lidar_setting_.cloud_coord);
    xtsdk->setCallback(eventCallback, imgCallback);

    xtsdk->startup();

    while (fwversionf <= 0.0 && !xtsdk->isconnect())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "wait for connection" << std::endl;
    }

    std::thread *checkThread = nullptr;
    if (addresstring.find("COM") != std::string::npos)
    {
        checkThread = new std::thread(checkConnection, addresstring);
    }
    else if (addresstring.find("tty") != std::string::npos)
    {
        checkThread = new std::thread(checkConnection, addresstring);
    }

    std::cout << "fwversionf " << fwversionf << std::endl;
    if (!checkFirmwareVersion(fwversionf, bin_path))
    {
        if (!xtsdk->updateFW(bin_path))
        {
            std::cout << "The firmware update failed." << std::endl;
            return 0;
        }
    }
    else
    {
        std::cout << "The firmware is up-to-date." << std::endl;
        return 0;
    }
    std::signal(SIGINT, signalHandler);
    std::thread worker([addresstring]()
                       {
    while (keepRunning)
    {
        // if(fwversionf > 0.0){
        //     xtsdk->setConnectIpaddress("COM3");
        // }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } });
    worker.join();
    if (checkThread)
        checkThread->join(); // 等待线程完成
    xtsdk->stop();
    xtsdk->setCallback();
    xtsdk->shutdown();
    return 0;
}
