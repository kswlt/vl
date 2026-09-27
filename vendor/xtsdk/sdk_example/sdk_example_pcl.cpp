#include <omp.h>
#include "../xtsdk/xtsdk.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>

#include <chrono>
#include <queue>
#include <iostream>
#include <fstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <vtkOutputWindow.h>
#include <pcl/point_types.h>
#include <pcl/visualization/pcl_visualizer.h>

// #include "../../xtsdk_cpp/xtsdk/xtsdk.h"
// #include "../../xtsdk_cpp/xtsdk/utils.h"

#include "para_example.h"
#include "pcl_render.h"

using namespace pcl::visualization;
using namespace XinTan;

std::thread *threadPcl;
static PCLVisualizer *pcl_viewer = nullptr;
pcl::PointCloud<pcl::PointXYZI>::Ptr lastcloud = nullptr;
pcl::PointCloud<pcl::PointXYZI>::Ptr lastcloudview = nullptr;
std::mutex cloudMutex;
bool xtpcl_filteron = false;
bool bHasNewcloud = false;
std::atomic<bool> keepRunning(true);
std::atomic<bool> pclRunning(true);
XtSdk::Ptr xtsdk;
para_example para_set;
int is_set_config = 0;

uint8_t is_connected = 0;
ExtrinsicIMULidar e_imu_lidar;
uint8_t imucount = 0;
void updatePcl(const std::shared_ptr<Frame> &frame);

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
                    std::cout << "******************* GET CONFIG SUCCESS *******************" << std::endl;
                    std::cout << "version: " << std::to_string(devconfig.version) << std::endl;
                    std::cout << "ImageType: " << ImageTypeStr[devconfig.imgType] << std::endl;
                    std::cout << "ModulationFreq: " << ModulationFreqStr[devconfig.modFreq] << std::endl;
                    std::cout << "HDRMode: " << HDRModeStr[devconfig.hdrMode] << std::endl;

                    std::cout << "integrationTimes: " << std::to_string(devconfig.integrationTimes[0]) << " "
                              << std::to_string(devconfig.integrationTimes[1]) << " "
                              << std::to_string(devconfig.integrationTimes[2]) << " "
                              << std::to_string(devconfig.integrationTimes[3]) << std::endl;
                    std::cout << "integrationTimeGs: " << std::to_string(devconfig.integrationTimeGs) << std::endl;
                    std::cout << "miniAmp: " << std::to_string(devconfig.miniAmp) << std::endl;
                    std::cout << "isFilterOn: " << std::to_string(devconfig.isFilterOn) << std::endl;
                    std::cout << "roi: " << std::to_string(devconfig.roi[0]) << " "
                              << std::to_string(devconfig.roi[1]) << " "
                              << std::to_string(devconfig.roi[2]) << " "
                              << std::to_string(devconfig.roi[3]) << std::endl;
                    std::cout << "maxfps: " << std::to_string(devconfig.maxfps) << std::endl;
                    std::cout << "bCompensateOn: " << std::to_string(devconfig.bCompensateOn) << std::endl;
                    std::cout << "bBinningH: " << std::to_string(devconfig.bBinningH) << std::endl;
                    std::cout << "bBinningV: " << std::to_string(devconfig.bBinningV) << std::endl;
                    std::cout << "freqChannel: " << std::to_string(devconfig.freqChannel) << std::endl;
                    std::cout << "setmaxfps: " << std::to_string(devconfig.setmaxfps) << std::endl;
                    std::cout << "endianType: " << std::to_string(devconfig.endianType) << std::endl;
                    std::cout << "freq: " << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[0]))] << " "
                              << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[1]))] << " "
                              << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[2]))] << " "
                              << ModulationFreqStr[getMultiFreq(static_cast<int>(devconfig.freq[3]))] << std::endl;
                    std::cout << "bcut_filteron: " << std::to_string(devconfig.bcut_filteron) << std::endl;
                    std::cout << "cut_intgrttime0: " << std::to_string(devconfig.cut_intgrttime0) << std::endl;
                    std::cout << "cut_distance0: " << std::to_string(devconfig.cut_distance0) << std::endl;
                    std::cout << "cut_intgrttime1: " << std::to_string(devconfig.cut_intgrttime1) << std::endl;
                    std::cout << "cut_distance1: " << std::to_string(devconfig.cut_distance1) << std::endl;
                    std::cout << "********************************************************" << std::endl;
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
                }
            }
            else
            {
                std::cout << "**********************SET REAL PARA***************************" << std::endl;
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
                std::cout << "start" << std::endl;
                xtsdk->setCutCorner(para_set.lidar_setting_.cut_corner);
                xtsdk->start((ImageType)para_set.lidar_setting_.imgType);
                std::cout << "********************************************************" << std::endl;
            }
            xtsdk->setBinningV(0); // 0为不设定binningv 1为设定binningv
            // xtsdk->setTransMirror(1, 1);//如果雷达绕深度方向旋180度安装，需设定此函数，保证点云方向正确
            xtsdk->setCutCorner(para_set.lidar_setting_.cut_corner);
            xtsdk->getImuExtParamters(e_imu_lidar, 1); // 获取imu to lidar外参 0为获取默认外参，1为获取标定外参，
                                                       // 如果需要setTransMirr需要设定 必须在设定之后 获取外参
            is_connected += 1;
            is_connected = is_connected > 10 ? 10 : is_connected;
        }
    }
    else if (event->eventstr == "devState")
    {
        std::cout << "devstate= " + xtsdk->getStateStr() << std::endl;
    }
    else
    {
        if (event->cmdid == 252)
        {
            if (is_connected == 0)
            {
                return;
            }
            // if (imucount > 10)
            // {
            // imucount = 0;
            FrameOutImu_t *pimu = (FrameOutImu_t *)(event->data.data());
            float imutemp = *(float *)&pimu->data[9];
            float imuaccx = *(float *)&pimu->data[0];
            float imuaccy = *(float *)&pimu->data[1];
            float imuaccz = *(float *)&pimu->data[2];
            float imugx = *(float *)&pimu->data[3];
            float imugy = *(float *)&pimu->data[4];
            float imugz = *(float *)&pimu->data[5];
            uint64_t imutimestamp = *(uint64_t *)&pimu->data[10];
            uint32_t times = 0;
            uint32_t timems = 0;
            if (imutimestamp > 100000000000) // ptp同步时
            {
                times = ((imutimestamp / 1000 - 37) % 60);
                timems = imutimestamp % 1000;
            }
            else
            {
                times = imutimestamp / 1000;
                timems = imutimestamp % 1000;
            }
            imucount++;
            // }

            if (imucount > 10)
            {
                std::cout << "linear_acceleration: " << imuaccx << " " << imuaccy << " " << imuaccz << std::endl;

                // std::cout << times << std::endl;
                // std::cout << timems << std::endl;
                imucount = 0;
            }
        }
    }
}

void imgCallback(const std::shared_ptr<Frame> &imgframe)
{
    // std::cout << "img: "+ std::to_string(imgframe->frame_id) << std::endl;
    if (imgframe->hasPointcloud)
        updatePcl(imgframe);

    // imgframe->points: 点云数据 ,
    // imgframe->distData : 深度数据， uint16_t类型，尺寸 imgframe->height*imgframe->width
    // imgframe->amplData : 信号幅度数据或者灰度数据， uint16_t类型, 尺寸 imgframe->height*imgframe->width
    // imgframe->distData : 深度数据， uint16_t类型，尺寸 imgframe->height*imgframe->width
}

#include <Eigen/Dense>


void updatePcl(const std::shared_ptr<Frame> &frame)
{
    std::cout << "img: " + std::to_string(frame->frame_id) << std::endl;

    const size_t nPixel = frame->width * frame->height;
    pcl::PointCloud<pcl::PointXYZI>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZI>());
    cloud->header.frame_id = "sensor_frame";
    cloud->header.stamp = frame->timeStampS * 1000000 + frame->timeStampNS;
    // cloud->header.stamp = frame->timeStampS * 1000 +frame->timeStampNS/1000000;
    cloud->width = static_cast<uint32_t>(frame->width);
    cloud->height = static_cast<uint32_t>(frame->height);
    cloud->is_dense = false;
    cloud->points.resize(nPixel);


    // 绕Y轴旋转公式 20度
    float angle_rad = 20 * M_PI / 180.0f;
    float cos_theta = cos(angle_rad);
    float sin_theta = sin(angle_rad);


    {
        int i = 0;
        for (i = 0; i < (int)nPixel; i++)
        {
            pcl::PointXYZI &p = cloud->points[i];
            p.x = frame->points[i].x;
            p.y = frame->points[i].y;
            p.z = frame->points[i].z;


            // 绕Y轴旋转公式 20度
            float x_old = p.x;
            float z_old = p.z;

            p.x = x_old * cos_theta + z_old * sin_theta;
            p.z = -x_old * sin_theta + z_old * cos_theta;


            p.intensity = frame->amplData[i] >= AMPLITUDE_ABNORMAL ? 0 : frame->amplData[i];
        }
    }

    {
        std::lock_guard<std::mutex> lock(cloudMutex); // 加锁
        lastcloud = cloud;                            // 安全更新点云
        bHasNewcloud = true;                          // 安全更新标志位
    }
}

void PclFunc()
{
    vtkOutputWindow::SetGlobalWarningDisplay(0);
    pcl_viewer = new PCLVisualizer("PointCloudViewer");

    pcl_viewer->setBackgroundColor(0.15, 0.15, 0.15);
    pcl_viewer->initCameraParameters();
    pcl_viewer->setShowFPS(false);
    pcl_viewer->setCameraPosition(0.0, 0, -12.0, 0, 0, 0, 0.0, 1.0, 0.0);

    {
        pcl::PointXYZ x_POS(0.4, 0.0, 0.0);
        pcl::PointXYZ y_POS(0.0, 0.4, 0.0);
        pcl::PointXYZ z_POS(0.0, 0.0, 0.4);
        pcl_viewer->addText3D("X", x_POS, 0.1, 1.0, 0.0, 0.0, "xid");
        pcl_viewer->addText3D("Y", y_POS, 0.1, 0.0, 1.0, 0.0, "yid");
        pcl_viewer->addText3D("Z", z_POS, 0.1, 0.0, 0.0, 1.0, "zid");
        pcl_viewer->addCoordinateSystem(0.3);
    }

    pcl::PointCloud<pcl::PointXYZI>::Ptr pcl_pointcloud(new pcl::PointCloud<pcl::PointXYZI>);
    pcl_viewer->addPointCloud<pcl::PointXYZI>(pcl_pointcloud, "xtlidar");
    pcl_viewer->setPointCloudRenderingProperties(PCL_VISUALIZER_POINT_SIZE, 3.0, "xtlidar");

    while (pclRunning && !pcl_viewer->wasStopped())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pcl::PointCloud<pcl::PointXYZI>::Ptr local_cloud;
        bool hasNew = false;
        {
            std::lock_guard<std::mutex> lock(cloudMutex);
            if (bHasNewcloud)
            {
                local_cloud = lastcloud;
                hasNew = true;
                bHasNewcloud = false;
            }
        }
        if (hasNew && local_cloud)
        {
            std::string render_type = para_set.lidar_setting_.renderType == 2 ? "intensity" : "z";
            pcl::visualization::PointCloudColorHandlerGenericField<pcl::PointXYZI> point_color_handle(local_cloud, render_type);
            pcl_viewer->updatePointCloud<pcl::PointXYZI>(local_cloud, point_color_handle, "xtlidar");
        }
        pcl_viewer->spinOnce();
    }
}

bool readIniFile(const std::string &filename,
                 para_example &para_)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(filename, pt);
        render_type = pt.get<int>("Setting.renderType", 0);

        // settings

        para_.lidar_setting_.freq = pt.get<int>("Setting.freq", 1);
        para_.lidar_setting_.HDR = pt.get<int>("Setting.HDR", 1);
        para_.lidar_setting_.imgType = pt.get<int>("Setting.imgType", 4);
        para_.lidar_setting_.cloud_coord = pt.get<int>("Setting.cloud_coord", 0);
        coordType = para_.lidar_setting_.cloud_coord;

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
        coordType = para_.lidar_setting_.cloud_coord;

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

        render_type = 0;
        coordType = 0;
        return false;
    }
}

void signalHandler(int signum)
{
    std::cout << "Received signal " << signum << ", exiting ..." << std::endl;
    if (threadPcl != nullptr)
    {
        pclRunning = false;
        keepRunning = false;
    }
}
#ifdef QT_FOUND
#include <QApplication>
#endif
#include "../xtsdk/utils.h"
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
    std::cout << "render type " << render_type << std::endl;

#ifdef QT_FOUND
    std::cout << "Qt FOUND and USING QT" << std::endl;
    QApplication a(argc, argv);
#endif
    xtsdk = std::make_shared<XtSdk>();
    std::string addresstring = "";
    if (argc <= 1)
    {
        std::cout << "Invalid param!" << std::endl;
        return 0;
    }
    else if (argc == 2)
    {
        addresstring = argv[1];
    }
    else
    {
        addresstring = argv[1];
        std::cout << "argv2: " << std::atoi(argv[2]) << std::endl;
        if (std::atoi(argv[2]) != 0 && std::atoi(argv[2]) != 1)
        {
            std::cout << "Invalid param 3! SET TO DEFAULT" << std::endl;
            // return 0;
        }
        else
        {
            is_set_config = std::atoi(argv[2]);
        }
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
    threadPcl = new std::thread(PclFunc);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    xtsdk->setCallback(eventCallback, imgCallback);
    if (para_set.lidar_filter_.reflectiveEnable)
    {
        xtsdk->setSdkReflectiveFilter(para_set.lidar_filter_.ref_th_min,
                                      para_set.lidar_filter_.ref_th_max);
    }
    if (para_set.lidar_filter_.edgeEnable)
    {
        xtsdk->setSdkEdgeFilter(para_set.lidar_filter_.edgeThreshold);
        // xtsdk->setSdkEdgeFilter(200);
    }
    if (para_set.lidar_filter_.kalmanEnable)
    {
        xtsdk->setSdkKalmanFilter(para_set.lidar_filter_.kalmanFactor * 1000, para_set.lidar_filter_.kalmanThreshold, 2000); // sdk中开启卡尔曼滤波
        // xtsdk->setSdkKalmanFilter(300, 300, 2000);
    }
    if (para_set.lidar_filter_.medianSize > 0)
    {
        xtsdk->setSdkMedianFilter(para_set.lidar_filter_.medianSize); // sdk中执行3*3的中值滤波
        // xtsdk->setSdkMedianFilter(3);
    }
    if (para_set.lidar_filter_.dustEnable)
    {
        std::cout << para_set.lidar_filter_.dustThreshold << std::endl;
        xtsdk->setSdkDustFilter(para_set.lidar_filter_.dustThreshold, para_set.lidar_filter_.dustFrames);

        // xtsdk->setSdkDustFilter(2002, 6);
    }
    if (para_set.lidar_filter_.postprocessEnable)
    {
        xtsdk->setPostProcess(para_set.lidar_filter_.postprocessThreshold,
                              static_cast<uint8_t>(para_set.lidar_filter_.dynamicsEnabled),
                              para_set.lidar_filter_.dynamicsWinsize,
                              para_set.lidar_filter_.dynamicsMotionsize);
    }
    if(para_set.lidar_filter_.spatialEnable)
    {
        xtsdk->setSpatialFilter(para_set.lidar_filter_.spatialAlpha,
                                para_set.lidar_filter_.spatialDelta,
                                para_set.lidar_filter_.spatialIterations);
    }
    if(para_set.lidar_filter_.averageEnable)
    {
        xtsdk->setSdkAvgFilter(para_set.lidar_filter_.averageSize, 2000);
    }

    xtsdk->startup();
    std::signal(SIGINT, signalHandler);
    std::thread worker([]()
                       {
                           while (keepRunning)
                           {
                               std::this_thread::sleep_for(std::chrono::seconds(1));
                           } });
    worker.join();
    if (threadPcl)
    {
        threadPcl->join(); // 等待PCL线程结束
        delete threadPcl;
        threadPcl = nullptr;
    }
    xtsdk->stop();
    xtsdk->setCallback();
    xtsdk->shutdown();
    std::cout << "Terminated " << std::endl;
    return 0;
}
