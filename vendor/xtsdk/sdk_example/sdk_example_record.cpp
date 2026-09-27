
#include "../xtsdk/utils.h"
#include "../xtsdk/xtsdk.h"

#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <para_example.h>
#include <string>
#include <thread>

using namespace XinTan;
namespace fs = boost::filesystem;

XtSdk::Ptr xtsdk;
std::atomic<bool> keepRunning(true);
para_example para_set;
int is_set_config = 0;
uint32_t currIndex = 0;
std::string xbin_path = "./";
std::string record_dir = "";
uint8_t connect_count = 0;
uint8_t is_connected_once = 0;

int getMultiFreq(const int &freq_dev)
{
    int index = 0;
    switch (freq_dev)
    {
    case 0:
        index = 2;
        break;
    case 1:
        index = 0;
        break;
    case 3:
        index = 1;
        break;
    case 4:
        index = 6;
        break;
    case 7:
        index = 3;
        break;
    case 15:
        index = 4;
        break;

    default:
        std::cout << "Unknown FREQ!" << std::endl;
        index = 100;
        break;
    }
    return index;
}
void eventCallback(const std::shared_ptr<CBEventData> &event)
{
    std::cout << "event: " + event->eventstr + " " + std::to_string(event->cmdid) << std::endl;

    if (event->eventstr == "sdkState")
    {
        if (xtsdk->isconnect() && (event->cmdid == 0xfe)) // 端口打开后第一次连接上设备
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
            if (fwversionf >= 2.20)
            {
                std::cout << "> 2.20" << std::endl;
                RespDevConfig devconfig;
                if (xtsdk->getDevConfig(devconfig))
                {
                    std::cout << "******************* GET CONFIG SUCCESS *******************"
                              << std::endl;
                    std::cout << "version: " << std::to_string(devconfig.version) << std::endl;
                    std::cout << "ImageType: " << ImageTypeStr[devconfig.imgType] << std::endl;
                    std::cout << "ModulationFreq: " << ModulationFreqStr[devconfig.modFreq]
                              << std::endl;
                    std::cout << "HDRMode: " << HDRModeStr[devconfig.hdrMode] << std::endl;

                    std::cout << "integrationTimes: "
                              << std::to_string(devconfig.integrationTimes[0]) << " "
                              << std::to_string(devconfig.integrationTimes[1]) << " "
                              << std::to_string(devconfig.integrationTimes[2]) << " "
                              << std::to_string(devconfig.integrationTimes[3]) << std::endl;
                    std::cout << "integrationTimeGs: "
                              << std::to_string(devconfig.integrationTimeGs) << std::endl;
                    std::cout << "miniAmp: " << std::to_string(devconfig.miniAmp) << std::endl;
                    std::cout << "isFilterOn: " << std::to_string(devconfig.isFilterOn)
                              << std::endl;
                    std::cout << "roi: " << std::to_string(devconfig.roi[0]) << " "
                              << std::to_string(devconfig.roi[1]) << " "
                              << std::to_string(devconfig.roi[2]) << " "
                              << std::to_string(devconfig.roi[3]) << std::endl;
                    std::cout << "maxfps: " << std::to_string(devconfig.maxfps) << std::endl;
                    std::cout << "bCompensateOn: " << std::to_string(devconfig.bCompensateOn)
                              << std::endl;
                    std::cout << "bBinningH: " << std::to_string(devconfig.bBinningH) << std::endl;
                    std::cout << "bBinningV: " << std::to_string(devconfig.bBinningV) << std::endl;
                    std::cout << "freqChannel: " << std::to_string(devconfig.freqChannel)
                              << std::endl;
                    std::cout << "setmaxfps: " << std::to_string(devconfig.setmaxfps) << std::endl;
                    std::cout << "endianType: " << std::to_string(devconfig.endianType)
                              << std::endl;
                    std::cout
                        << "freq: "
                        << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[0]))]
                        << " "
                        << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[1]))]
                        << " "
                        << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[2]))]
                        << " "
                        << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[3]))]
                        << std::endl;
                    std::cout << "bcut_filteron: " << std::to_string(devconfig.bcut_filteron)
                              << std::endl;
                    std::cout << "cut_intgrttime0: " << std::to_string(devconfig.cut_intgrttime0)
                              << std::endl;
                    std::cout << "cut_distance0: " << std::to_string(devconfig.cut_distance0)
                              << std::endl;
                    std::cout << "cut_intgrttime1: " << std::to_string(devconfig.cut_intgrttime1)
                              << std::endl;
                    std::cout << "cut_distance1: " << std::to_string(devconfig.cut_distance1)
                              << std::endl;
                    std::cout << "********************************************************"
                              << std::endl;
                }
                if (is_set_config == 1)
                {
                    std::cout << "**********************SET REAL PARA***************************"
                              << std::endl;
                    xtsdk->setHdrMode((HDRMode)para_set.lidar_setting_.HDR);
                    xtsdk->setIntTimesus(para_set.lidar_setting_.intgs,
                                         para_set.lidar_setting_.int1, para_set.lidar_setting_.int2,
                                         para_set.lidar_setting_.int3,
                                         para_set.lidar_setting_.int4);
                    xtsdk->setMinAmplitude(para_set.lidar_setting_.minLSB);
                    xtsdk->setMaxFps(para_set.lidar_setting_.maxfps);
                    xtsdk->setMultiModFreq((ModulationFreq)para_set.lidar_setting_.freq1,
                                           (ModulationFreq)para_set.lidar_setting_.freq2,
                                           (ModulationFreq)para_set.lidar_setting_.freq3,
                                           (ModulationFreq)para_set.lidar_setting_.freq4);
                    xtsdk->setCutCorner(para_set.lidar_setting_.cut_corner);
                    xtsdk->start((ImageType)para_set.lidar_setting_.imgType);
                }
                else
                {
                    xtsdk->setCutCorner(para_set.lidar_setting_.cut_corner);
                    xtsdk->start((ImageType)para_set.lidar_setting_.imgType);
                    // xtsdk->start((ImageType)devconfig.imgType);
                    return;
                }
            }
            else
            {
                std::cout << "**********************SET REAL PARA***************************"
                          << std::endl;
                xtsdk->setModFreq((ModulationFreq)para_set.lidar_setting_.freq);
                xtsdk->setHdrMode((HDRMode)para_set.lidar_setting_.HDR);
                xtsdk->setIntTimesus(para_set.lidar_setting_.intgs, para_set.lidar_setting_.int1,
                                     para_set.lidar_setting_.int2, para_set.lidar_setting_.int3,
                                     para_set.lidar_setting_.int4);
                xtsdk->setMinAmplitude(para_set.lidar_setting_.minLSB);
                xtsdk->setMaxFps(para_set.lidar_setting_.maxfps);
                xtsdk->setMultiModFreq((ModulationFreq)para_set.lidar_setting_.freq1,
                                       (ModulationFreq)para_set.lidar_setting_.freq2,
                                       (ModulationFreq)para_set.lidar_setting_.freq3,
                                       (ModulationFreq)para_set.lidar_setting_.freq4);
                xtsdk->setCutCorner(para_set.lidar_setting_.cut_corner);
                xtsdk->start((ImageType)para_set.lidar_setting_.imgType);
                std::cout << "********************************************************"
                          << std::endl;
            }
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
        std::cout << "event: " + event->eventstr + " cmd=" << std::to_string(event->cmdid)
                  << std::endl;
    }
}

void imgCallback(const std::shared_ptr<Frame> &imgframe)
{
    std::cout << "img: " + std::to_string((int)(imgframe->frame_id))
              << " size: " + std::to_string((int)(imgframe->points.size())) << std::endl;

    uint8_t i = 0;
    CamParameterS g_camparams;
    std::cout << "currIndex: " << currIndex << std::endl;
    if (currIndex == 0)
    {
        while (i <= 8 && !xtsdk->getLensCalidata(g_camparams))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            i++;
        }
        if (i > 8)
        {
            std::cout << "ERROR: get lens calib data failed" << std::endl;
            return;
        }
        record_dir = xbin_path + "/" + Utils::getTimeStr() + "/";
        if (fs::create_directory(record_dir))
        {
            std::cout << "Directory created successfully: " << record_dir << std::endl;
        }
        else
        {
            std::cout << "Directory already exists or could not be created." << std::endl;
            return;
        }
    }
    currIndex++;

    xtsdk->doXbinRecord(record_dir, imgframe, currIndex);
}

bool readIniFile(const std::string &filename,
                 para_example &para_)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(filename, pt);

        // settings

        para_.lidar_setting_.freq = pt.get<int>("Setting.freq", 1);
        para_.lidar_setting_.HDR = pt.get<int>("Setting.HDR", 1);
        para_.lidar_setting_.imgType = pt.get<int>("Setting.imgType", 4);
        para_.lidar_setting_.cloud_coord = pt.get<int>("Setting.cloud_coord", 0);

        para_.lidar_setting_.int1 = pt.get<int>("Setting.int1", 100);
        para_.lidar_setting_.int2 = pt.get<int>("Setting.int2", 1000);
        para_.lidar_setting_.int3 = pt.get<int>("Setting.int3", 0);
        para_.lidar_setting_.int4 = pt.get<int>("Setting.int4", 0);
        para_.lidar_setting_.intgs = pt.get<int>("Setting.intgs", 2000);

        para_.lidar_setting_.freq1 = pt.get<int>("Setting.freq1", 2);
        para_.lidar_setting_.freq2 = pt.get<int>("Setting.freq2", 0);
        para_.lidar_setting_.freq3 = pt.get<int>("Setting.freq3", 1);
        para_.lidar_setting_.freq4 = pt.get<int>("Setting.freq4", 1);
        para_.lidar_setting_.minLSB = pt.get<int>("Setting.minLSB", 80);
        para_.lidar_setting_.cut_corner = pt.get<int>("Setting.cut_corner", 60);

        para_.lidar_setting_.start_stream = pt.get<bool>("Setting.start_stream", true);
        para_.lidar_setting_.connect_address = pt.get<std::string>("Setting.connect_address", "192.168.0.101");

        para_.lidar_setting_.maxfps = pt.get<int>("Setting.maxfps", 30);
        para_.lidar_setting_.renderType = pt.get<int>("Setting.renderType", 2);

        para_.lidar_setting_.hmirror = pt.get<bool>("Setting.hmirror", false);
        para_.lidar_setting_.vmirror = pt.get<bool>("Setting.vmirror", false);

        // filters
        para_.lidar_filter_.medianSize = pt.get<int>("Filters.medianSize", 5);
        para_.lidar_filter_.kalmanEnable = pt.get<int>("Filters.kalmanEnable", 1);
        para_.lidar_filter_.kalmanFactor = pt.get<float>("Filters.kalmanFactor", 0.4);
        para_.lidar_filter_.kalmanThreshold = pt.get<int>("Filters.kalmanThreshold", 400);
        para_.lidar_filter_.edgeEnable = pt.get<bool>("Filters.edgeEnable", true);
        para_.lidar_filter_.edgeThreshold = pt.get<int>("Filters.edgeThreshold", 200);
        para_.lidar_filter_.dustEnable = pt.get<bool>("Filters.dustEnable", true);
        para_.lidar_filter_.dustThreshold = pt.get<int>("Filters.dustThreshold", 2000);
        para_.lidar_filter_.dustFrames = pt.get<int>("Filters.dustFrames", 2);
        para_.lidar_filter_.postprocessEnable = pt.get<bool>("Filters.postprocessEnable", true);
        para_.lidar_filter_.dynamicsEnabled = pt.get<bool>("Filters.dynamicsEnabled", true);
        para_.lidar_filter_.dynamicsWinsize = pt.get<int>("Filters.dynamicsWinsize", 9);
        para_.lidar_filter_.reflectiveEnable = pt.get<bool>("Filters.reflectiveEnable", true);
        para_.lidar_filter_.ref_th_min = pt.get<float>("Filters.ref_th_min", 0.5);
        para_.lidar_filter_.ref_th_max = pt.get<float>("Filters.ref_th_max", 2.0);
        para_.lidar_filter_.postprocessThreshold = pt.get<float>("Filters.postprocessThreshold", 5.0);
        para_.lidar_filter_.spatialEnable = pt.get<bool>("Filters.spatialEnable", true);
        para_.lidar_filter_.spatialAlpha = pt.get<float>("Filters.spatialAlpha", 0.7);
        para_.lidar_filter_.spatialDelta = pt.get<int>("Filters.spatialDelta", 70);
        para_.lidar_filter_.spatialIterations = pt.get<int>("Filters.spatialIterations", 2);
        para_.lidar_filter_.averageEnable = pt.get<bool>("Filters.averageEnable", false);
        para_.lidar_filter_.averageSize = pt.get<int>("Filters.averageSize", 4);

        return true;
    }
    catch (const std::exception &e)
    {
        std::cout << "Error reading ini file" << std::endl;

        para_.lidar_setting_.freq = 1;
        para_.lidar_setting_.HDR = 1;
        para_.lidar_setting_.imgType = 4;
        para_.lidar_setting_.cloud_coord = 0;

        para_.lidar_setting_.int1 = 100;
        para_.lidar_setting_.int2 = 1000;
        para_.lidar_setting_.int3 = 0;
        para_.lidar_setting_.int4 = 0;
        para_.lidar_setting_.intgs = 2000;

        para_.lidar_setting_.freq1 = 2;
        para_.lidar_setting_.freq2 = 0;
        para_.lidar_setting_.freq3 = 1;
        para_.lidar_setting_.freq4 = 1;
        para_.lidar_setting_.minLSB = 80;
        para_.lidar_setting_.cut_corner = 60;
        para_.lidar_setting_.renderType = 2;

        para_.lidar_setting_.start_stream = true;
        para_.lidar_setting_.connect_address = "192.168.0.101";

        para_.lidar_setting_.maxfps = 30;

        para_.lidar_setting_.hmirror = false;
        para_.lidar_setting_.vmirror = false;

        // filters
        para_.lidar_filter_.medianSize = 5;
        para_.lidar_filter_.kalmanEnable = 0;
        para_.lidar_filter_.kalmanFactor = 0.4;
        para_.lidar_filter_.kalmanThreshold = 400;
        para_.lidar_filter_.edgeEnable = true;
        para_.lidar_filter_.edgeThreshold = 200;
        para_.lidar_filter_.dustEnable = true;
        para_.lidar_filter_.dustThreshold = 2000;
        para_.lidar_filter_.dustFrames = 2;
        para_.lidar_filter_.postprocessEnable = true;
        para_.lidar_filter_.dynamicsEnabled = true;
        para_.lidar_filter_.dynamicsWinsize = 9;
        para_.lidar_filter_.reflectiveEnable = true;
        para_.lidar_filter_.ref_th_min = 0.5;
        para_.lidar_filter_.ref_th_max = 2.0;
        para_.lidar_filter_.postprocessThreshold = 5.0;
        para_.lidar_filter_.spatialEnable = true;
        para_.lidar_filter_.spatialAlpha =  0.7;
        para_.lidar_filter_.spatialDelta = 70;
        para_.lidar_filter_.spatialIterations = 2;
        para_.lidar_filter_.averageEnable = false;
        para_.lidar_filter_.averageSize = 4;

        return false;
    }
}

void signalHandler(int signum)
{
    std::cout << "Received signal " << signum << ", exiting ..." << std::endl;
    keepRunning = false;
}
int main(int argc, char *argv[])
{
    std::string cfg_path = std::string(EXAMPLE_DIR) + "/sdk_example/cfg/xintan.xtcfg";
    if (readIniFile(cfg_path, para_set))
    {
        std::cout << "Read ini file success!" << std::endl;
    }
    else
    {
        std::cout << "Read ini file failed!" << std::endl;
        // return -1;
    }

    xtsdk = std::make_shared<XtSdk>();

    std::string addresstring = "";

    switch (argc)
    {
    case 1:
        std::cout << "Invalid param!" << std::endl;
        return 0;
    case 2:
        addresstring = argv[1];
        break;
    case 3:
        addresstring = argv[1];
        xbin_path = argv[2];
        break;
    case 4:
        addresstring = argv[1];
        xbin_path = argv[2];
        if (std::atoi(argv[3]) != 0 && std::atoi(argv[3]) != 1)
        {
            std::cout << "Invalid param 3! SET TO DEFAULT" << std::endl;
        }
        else
        {
            is_set_config = std::atoi(argv[3]);
        }
        break;

    default:
        break;
    }

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

    std::signal(SIGINT, signalHandler);
    std::thread worker([]()
                       {
        while (keepRunning) {

            if (is_connected_once < connect_count) {
                std::cout << "***********************DEVICE RECONNECTED***********************"
                          << std::endl;
                connect_count = connect_count > 10 ? 10 : connect_count;
                is_connected_once = connect_count;
                currIndex = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } });
    worker.join();

    xtsdk->stop();
    xtsdk->setCallback();
    xtsdk->shutdown();
    return 0;
}
