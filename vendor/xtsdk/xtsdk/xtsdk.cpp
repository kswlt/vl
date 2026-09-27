/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/

#include "xtsdk.h"
#include "utils.h"
#include "xtlogger.h"
#include "xtdaemon.h"
#include "version.h"
#include <fstream>
#include <algorithm>

const uint8_t CMD_TEST = 0;
const uint8_t CMD_START = 1;
const uint8_t CMD_STOP = 2;
const uint8_t CMD_SET_IP = 3;
const uint8_t CMD_GET_DEVINFO = 4;
const uint8_t CMD_GET_CONFIG = 5;
const uint8_t CMD_SET_FILTER = 6;
const uint8_t CMD_SET_TIMESYNC = 7;
const uint8_t CMD_SET_INTEGRATIONTIME = 8;
const uint8_t CMD_SET_MINAMP = 9;
const uint8_t CMD_SET_HDR = 10;
const uint8_t CMD_RESET = 13;
const uint8_t CMD_RESTORE_DEFAULT = 14;
const uint8_t CMD_GET_LENSDATA = 18;
const uint8_t CMD_SET_UDPIP = 19;
const uint8_t CMD_SET_ROI = 51;
const uint8_t CMD_SET_MODFREQ = 52;
const uint8_t CMD_SET_MAXFPS = 56;

const std::string sdkStateStr[] = {
    "NotStartup",
    "PortOpening",
    "TxRxVerifying",
    "UDP Ok",
    "Connected",
    "Unknow"};

const std::string devStateStr[] = {
    "Disconnected",
    "Init",
    "Idle",
    "Stream",
    "ErrCSI",
    "ErrI2C",
    "ErrTEMPL",
    "WarnTEMP",
    "tempDownFreq",
    "ErrTEMPH",
    "NoSensor",
    "NoModIc",
    "NoTempIc",
    "BootNoApp",
    "Bootloader",
    "Unknow",

    "ErrPowerV",
    "ErrPowerA",
    "ErrRAM",
    "ErrFlash",
    "ErrNoCalibFinish",
    "ErrNoCamParamters",
    "ErrNoMacCfg",
    "ErrNoSnCfg",
    "ErrTempParamters",
    "Calibrating",
    "ErrNoImuParamters",
    "ErrUnknowMax"};

#define XTDAEMONUSING XtDaemon *xtdaemon = (XtDaemon *)pInteranl

// 录制时里面包含的配置数据
struct RecordCfgINFO_V2
{
    uint8_t modFreq;
    uint8_t hdrMode;
    uint8_t isFilterOn;
    uint8_t maxfps;
    uint16_t miniAmp;
    uint16_t integrationTimes[3];
    uint16_t integrationTimeGs;
    char snstr[40];
    char fwstr[20];
};

#pragma pack(push, 1) // 设置字节对齐为 1
struct FrameInfoOld_t
{
    uint16_t temperature;
    uint16_t roi_x0;
    uint16_t roi_y0;
    uint8_t binning;
    uint8_t reduce;
    uint8_t unit;
    uint16_t vcseltemperature;

    uint8_t reserver[27];

    uint8_t timestamp[10];
    uint8_t timeStampState;
    uint8_t timeStampType;
    uint8_t statecode;
    uint8_t version;
};
#pragma pack(pop) // 恢复原始的对齐方式

namespace XinTan
{

    XtSdk::XtSdk(const std::string logpath, const std::string logtag, void *pxt)
    {
        printVersionBanner();
        logtagname = logtag;
        init_logger(logpath);

        XtDaemon *xtdaemon = new XtDaemon(logtagname);
        pInteranl = xtdaemon;
        xtdaemon->devStateStr = devStateStr;

        xtdaemon->setEvtInternalProcCallbk(eventInternalProcess, this);
        if (pxt != nullptr)
            *((XtDaemon **)pxt) = xtdaemon;
    }

    XtSdk::~XtSdk()
    {
        XTDAEMONUSING;
        delete xtdaemon;
        pInteranl = nullptr;
        XTLOGINFO("sdk delete");
    }

    void XtSdk::printVersionBanner()
    {
        std::cout << "=================================\n";
        std::cout << "  " << APP_NAME << " " << VERSION_STRING << "\n";
        std::cout << "=================================\n\n";
    }

    /***********  SDK 必要设置 **********************/
    void XtSdk::setCallback(std::function<void(const std::shared_ptr<CBEventData> &)> eventcallback,
                            std::function<void(const std::shared_ptr<Frame> &)> imgcallback)
    {
        XTDAEMONUSING;
        xtdaemon->setCallback(eventcallback, imgcallback);
    }

    void XtSdk::setCallback(std::function<void(const std::shared_ptr<CBEventData> &, void *)> eventcallback,
                            std::function<void(const std::shared_ptr<Frame> &, void *)> imgcallback, void *peventIn, void *pimgIn)
    {
        XTDAEMONUSING;
        xtdaemon->setCallback(eventcallback, imgcallback, peventIn, pimgIn);
    }

    bool XtSdk::setConnectIpaddress(std::string ipAddress)
    {
        XTDAEMONUSING;
        return xtdaemon->setConnectAddress(ipAddress);
    }

    bool XtSdk::setConnectSerialportName(std::string serialportName)
    {
        XTDAEMONUSING;
        return xtdaemon->setConnectAddress(serialportName);
    }

    bool XtSdk::isUsedNet()
    {
        XTDAEMONUSING;
        return xtdaemon->isSelNet;
    }

    bool XtSdk::set_endianCheck(bool enable)
    {
        XTDAEMONUSING;
        xtdaemon->set_endianCheck(enable);
        return true;
    }

    /***********  SDK 运行相关 **********************/
    void XtSdk::startup()
    {
        XTDAEMONUSING;
        xtdaemon->startup();
    }

    void XtSdk::shutdown()
    {
        XTDAEMONUSING;
        xtdaemon->shutdown();
        XTLOGINFO("xtsdk Shutdown completed successfully");
    }

    bool XtSdk::isconnect()
    {
        XTDAEMONUSING;
        if (xtdaemon->sdkState == STATE_CONNECTED)
            return true;
        else
            return false;
    }

    SdkState XtSdk::getSdkState()
    {
        XTDAEMONUSING;
        return xtdaemon->sdkState;
    }

    DevStateCode XtSdk::getDevState()
    {
        XTDAEMONUSING;
        return xtdaemon->devState;
    }

    std::string XtSdk::getStateStr()
    {
        XTDAEMONUSING;
        std::string statestr = sdkStateStr[xtdaemon->sdkState];
        if (xtdaemon->devState < DevSTATE_ERR_MAX)
            statestr.append("-" + devStateStr[xtdaemon->devState]);
        else
            statestr.append("-Unknow:" + std::to_string(xtdaemon->devState));
        return statestr;
    }

    bool XtSdk::getLensCalidata(CamParameterS &camparameter)
    {
        XByteArray data = {};

        XByteArray respData;
        if (customCmd(CMD_GET_LENSDATA, data, respData))
        {
            if (respData.size() == sizeof(camparameter))
            {
                memcpy(&camparameter, respData.data(), sizeof(camparameter));
                return true;
            }
        }
        return false;
    }

    bool XtSdk::getImuExtParamters(ExtrinsicIMULidar &imuparameters, uint8_t flag)
    {
        XTDAEMONUSING;

        if (flag == 0)
        {
            imuparameters = xtdaemon->cartesianTransform->getCurrentImuExtParamters();
            return true;
        }
        XByteArray data = {};

        XByteArray respData;
        if (customCmd(29, data, respData))
        {
            if (respData.size() == sizeof(imuparameters))
            {
                ExtrinsicIMULidar ext_tmp;
                memcpy(&ext_tmp, respData.data(), sizeof(imuparameters));
                xtdaemon->cartesianTransform->updateImuExtParamters(ext_tmp);
                imuparameters = xtdaemon->cartesianTransform->getCurrentImuExtParamters();
                return true;
            }
        }
        else
        {
            imuparameters = xtdaemon->cartesianTransform->getCurrentImuExtParamters();
            return true;
        }
        return false;
    }

    void XtSdk::setTransMirror(bool hmirror, bool vmirror)
    {
        XTDAEMONUSING;
        xtdaemon->cartesianTransform->setTransMirror(hmirror, vmirror);
    }

    void XtSdk::setPointsCornerCut(bool iscut)
    {
        XTDAEMONUSING;
        xtdaemon->cartesianTransform->bPointsCornerCut = iscut;
    }

    void XtSdk::setPointsLsbCut(uint16_t minAmp)
    {
        XTDAEMONUSING;
        xtdaemon->cartesianTransform->cutMinAmp = minAmp;
    }

    extern uint16_t g_xbinning;
    extern uint16_t g_ybinning;

    void XtSdk::setDownSample(uint16_t x, uint16_t y)
    {
        XTDAEMONUSING;
        g_xbinning = x;
        g_ybinning = y;
    }

    void XtSdk::setCutCorner(uint32_t cutvalue)
    {
        XTDAEMONUSING;
        xtdaemon->cartesianTransform->setcutcorner(cutvalue);
    }

    void XtSdk::setPostProcess(const float &dilation_pixels, const uint8_t &mode, uint8_t winsize, uint8_t motion_size)
    {
        XTDAEMONUSING;
        xtdaemon->baseFilter->setPostParam(dilation_pixels, mode, winsize, motion_size);
    }

    bool XtSdk::setSdkKalmanFilter(uint16_t factor, uint16_t threshold, uint16_t timedf)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setKalmanFilter(factor, threshold, timedf);
    }

    bool XtSdk::setSdkEdgeFilter(uint16_t threshold)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setEdgeFilter(threshold);
    }

    bool XtSdk::setSdkMedianFilter(uint16_t size)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setMedianFilter(size);
    }

    bool XtSdk::setSdkReflectiveFilter(const float &threshold_min, const float &threshold_max)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setSdkReflectiveFilter(threshold_min, threshold_max);
    }

    bool XtSdk::setSdkDustFilter(uint16_t threshold, uint16_t framecount, uint16_t timedf, uint16_t validpercent)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setDustFilter(threshold, framecount, validpercent, timedf);
    }

    bool XtSdk::setSpatialFilter(const float &alpha, const uint32_t &delta, const uint8_t &iterations)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setSpatialFilter(alpha, delta, iterations);
    }

    bool XtSdk::setSdkAvgFilter(uint16_t size, uint16_t timedf)
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->setAvgFilter(size, timedf);
    }

    void XtSdk::resetFilters()
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->resetFilters();
    }

    bool XtSdk::clearAllSdkFilter()
    {
        XTDAEMONUSING;
        return xtdaemon->baseFilter->clearAllFilter();
    }

    bool XtSdk::setSdkCloudCoordType(ClOUDCOORD_TYPE type)
    {
        XTDAEMONUSING;
        return xtdaemon->cartesianTransform->pointout_coord = type;
    }

    extern float g_reflectcoef;
    extern uint32_t g_reflectcutmax;

    void XtSdk::setReflectivityCoef(float coef)
    {
        g_reflectcoef = coef;
    }

    void XtSdk::setSdkCutMaxRefFilter(uint32_t cutmaxref)
    {
        g_reflectcutmax = cutmaxref;
    }

    /***********  命令相关 API **********************/

    bool XtSdk::testDev()
    {
        XTDAEMONUSING;
        RespResult respResult = xtdaemon->transceiveCmd(CMD_TEST, {});
        if (respResult.ret_code == CmdResp_OK)
        {
            return true;
        }
        return false;
    }

    bool XtSdk::start(ImageType imgType, bool isOnce)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(imgType));

        uint8_t tindex = 0;
        uint8_t repeat = 0;
        bool isPointCloud = false;

        if (imgType == IMG_DISTANCE)
            tindex = 1;
        else if (imgType == IMG_AMPLITUDE)
        {
            tindex = 2;
        }
        else if (imgType == IMG_GRAYSCALE)
            tindex = 3;
        else if (imgType == IMG_POINTCLOUD)
        {
            isPointCloud = true;
            tindex = 1;
        }
        else if (imgType == IMG_POINTCLOUDAMP)
        {
            isPointCloud = true;
            tindex = 2;
        }
        else
            return false;

        xtdaemon->needPointcloud = isPointCloud;

        repeat = isOnce ? 0 : 1;
        XByteArray data = {tindex, repeat};
        RespResult respResult = xtdaemon->transceiveCmd(CMD_START, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::stop()
    {
        XTDAEMONUSING;
        XTLOGINFO("");

        RespResult respResult = xtdaemon->transceiveCmd(CMD_STOP, {});

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::updateFW(const std::string &bin_path)
    {
        XTDAEMONUSING;
        if (!Utils::hasExtension(bin_path, ".bin"))
        {
            std::cout << "read bin file failed: " << bin_path << std::endl;
            return false;
        }

        std::ifstream file(bin_path, std::ios::binary | std::ios::ate);

        uint32_t filetype = 0;
        if (!file.is_open())
        {
            std::cout << "Failed to open file: " << bin_path << std::endl;
            return false;
        }
        // 获取文件大小
        std::streamsize filesize = file.tellg();
        file.seekg(0, std::ios::beg);

        // 分配缓冲区
        char *pBuff = new char[filesize];

        // 读取文件数据
        if (file.read(pBuff, filesize))
        {
            // 等待 1 秒（如果需要）
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // 从缓冲区读取文件类型
            uint8_t *pfiledata = reinterpret_cast<uint8_t *>(pBuff);
            filetype = (pfiledata[3] << 24) | (pfiledata[2] << 16) | (pfiledata[1] << 8) | pfiledata[0];

            std::cout << "Filetype: " << std::hex << filetype << std::endl;
        }
        else
        {
            // 释放缓冲区
            delete[] pBuff;
            std::cout << "Failed to read the file: " << bin_path << std::endl;
            return false;
        }

        file.close();

        if (filetype == 710111)
        {
        }
        else
        {
            XByteArray respData;
            customCmd(48, {}, respData);

            int count = 0;
            while (count < 30)
            {
                count++;

                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                uint8_t devstate = getDevState();
                std::cout << "current devstate " << devstate << std::endl;
                if ((devstate == 0x0D) || (devstate == 0x0E)) // bootnoapp or boot
                {
                    break;
                }
            }
            if (count > 28)
            {
                delete[] pBuff;
                std::cout << "Failed to enter bootloader" << std::endl;
                return false;
            }
        }

        uint8_t bufferSend[2000];

        // 发送尺寸
        bufferSend[0] = 0x01;
        Utils::setValueUint32Endian(bufferSend + 1, filesize, getEndianType());

        XByteArray data;

        data.assign(bufferSend, bufferSend + 5);

        XByteArray respData;
        if (customCmd(49, data, respData, 4000) == false)
        {

            std::cout << "Failed to writ fw data" << std::endl;
            delete[] pBuff;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        uint8_t *pdata = (uint8_t *)pBuff;
        int oncecount = 0;

        uint16_t sn = 0;
        int pkgcount = filesize / 1024;
        if (filesize % 1024)
            pkgcount++;

        // 发送数据
        for (int i = 0; i < filesize;)
        {
            oncecount = 1024;
            if ((i + 1024) > filesize)
                oncecount = filesize - i;

            bufferSend[0] = 0x02;
            bufferSend[1] = (sn >> 8) & 0x00FF;
            bufferSend[2] = sn & 0x00FF;
            memcpy(bufferSend + 3, pdata, oncecount);

            XByteArray data2;

            data2.assign(bufferSend, bufferSend + oncecount + 3);

            XByteArray respData2;
            if (customCmd(49, data2, respData2, 4000))
            {
                pdata += 1024;
            }
            else
            {
                std::cout << "write calidata failed" << std::to_string(sn) << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            sn++;
            i += oncecount;
            float percentage = (static_cast<float>(sn) / pkgcount) * 100;
            std::cout << "Progress: " << percentage << "%" << std::endl;
        }

        // 发送CRC 结束
        bufferSend[0] = 0x03;

        XByteArray data1;

        data1.assign(bufferSend, bufferSend + 1);

        XByteArray respData1;
        customCmd(49, data1, respData1, 4000);

        if (sn == pkgcount)
        {
            std::cout << "fwupdata: Write ok " << std::endl;
        }
        else
        {
            std::cout << "fwupdata: Write failed " << std::to_string(sn) + " " + std::to_string(pkgcount) << std::endl;
        }
        return true;
    }

    bool XtSdk::getDevInfo(RespDevInfo &devInfo)
    {
        XTDAEMONUSING;
        RespResult respResult =
            xtdaemon->transceiveCmd(CMD_GET_DEVINFO, {}, 2000);
        if (respResult.ret_code != CmdResp_OK)
        {
            std::cout << "getdevinfo=" << std::to_string(respResult.ret_code)
                      << std::endl;
            XTLOGINFO("getdevinfo=" + std::to_string(respResult.ret_code));
            devInfo.fwVersion = "read failed";
            return false;
        }

        // std::cout << respResult.data.size() << " info  " << sizeof(struct
        // DevInfo_t) << std::endl;
        devInfo.bdrnulens = 0;

        if (respResult.data.size() == sizeof(struct DevInfo_t))
        {
            struct DevInfo_t rawdevinfo;
            memcpy(&rawdevinfo, respResult.data.data(), sizeof(rawdevinfo));

            devInfo.sn = (char *)rawdevinfo.sn;
            devInfo.fwVersion = (char *)(rawdevinfo.fw);
            devInfo.bootVersion = (char *)rawdevinfo.bootver;
            devInfo.chipidStr = std::to_string(rawdevinfo.chipid) + " " +
                                std::to_string(rawdevinfo.waferid);

            memcpy(devInfo.ip, rawdevinfo.ip, 12);
            memcpy(devInfo.mac, rawdevinfo.mac, 6);
            devInfo.chipid[0] = rawdevinfo.chipid;
            devInfo.chipid[1] = rawdevinfo.waferid;
            devInfo.isCalibrated[0] = rawdevinfo.calibrated;
            devInfo.isCalibrated[1] = rawdevinfo.calibrated;
            devInfo.isCalibrated[2] = rawdevinfo.calibrated;

            Utils::setValueUint32Endian(
                devInfo.udpDestIp, rawdevinfo.udpDestIp, Endian_Little);
            devInfo.udpDestPort = rawdevinfo.udpDestPort;
            devInfo.timeSyncType = rawdevinfo.timesync_type;

            //if ((rawdevinfo.otherflags & 0x01) && ((rawdevinfo.otherflags & (0x01 << 4)) > 0))
            //    devInfo.bdrnulens = 1;
            devInfo.bdrnulens = rawdevinfo.otherflags;

            return true;
        }
        else
        {
            if (respResult.data.size() > 79)
            {
                std::ostringstream osfw;
                std::ostringstream ossn;
                std::ostringstream osbtver;

                XByteArray data;
                int offset = 0;
                devInfo.ip[0] = respResult.data[0];
                devInfo.ip[1] = respResult.data[1];
                devInfo.ip[2] = respResult.data[2];
                devInfo.ip[3] = respResult.data[3];
                offset += 4;
                if (respResult.data.size() > 83)
                {
                    devInfo.ip[4] = respResult.data[4];
                    devInfo.ip[5] = respResult.data[5];
                    devInfo.ip[6] = respResult.data[6];
                    devInfo.ip[7] = respResult.data[7];

                    devInfo.ip[8] = respResult.data[8];
                    devInfo.ip[9] = respResult.data[9];
                    devInfo.ip[10] = respResult.data[10];
                    devInfo.ip[11] = respResult.data[11];
                    offset += 8;
                }

                devInfo.mac[0] = respResult.data[offset + 0];
                devInfo.mac[1] = respResult.data[offset + 1];
                devInfo.mac[2] = respResult.data[offset + 2];
                devInfo.mac[3] = respResult.data[offset + 3];
                devInfo.mac[4] = respResult.data[offset + 4];
                devInfo.mac[5] = respResult.data[offset + 5];

                offset += 6;
                data.assign(respResult.data.begin() + offset,
                            respResult.data.begin() + 18 + offset);
                osfw << data.data();
                devInfo.fwVersion = osfw.str();

                offset += 18;

                int strlen = 28;
                for (int i = 0; i < 28; i++)
                {
                    if (respResult.data[offset + i] == 0)
                    {
                        strlen = i;
                        break;
                    }
                }
                data.assign(respResult.data.begin() + offset,
                            respResult.data.begin() + strlen + offset);
                data.push_back(0x00);
                ossn << data.data();
                devInfo.sn = ossn.str();

                offset += 28;

                strlen = 14;
                for (int i = 0; i < 14; i++)
                {
                    if (respResult.data[offset + i] == 0)
                    {
                        strlen = i;
                        break;
                    }
                }
                data.assign(respResult.data.begin() + offset,
                            respResult.data.begin() + strlen + offset);
                data.push_back(0x00);
                osbtver << data.data();

                devInfo.bootVersion = osbtver.str();
                offset += 15;

                data.assign(respResult.data.begin() + offset,
                            respResult.data.begin() + 2 + offset);
                uint16_t chipid = (data[0] << 8) + data[1];
                data.assign(respResult.data.begin() + offset + 2,
                            respResult.data.begin() + 4 + offset);
                uint16_t waferid = (data[0] << 8) + data[1];
                devInfo.chipidStr =
                    std::to_string(chipid) + " " + std::to_string(waferid);
                devInfo.chipid[0] = chipid;
                devInfo.chipid[1] = waferid;
                offset += 4;

                if (respResult.data.size() > 88)
                {
                    offset += 4;
                    devInfo.isCalibrated[0] = respResult.data[offset];
                    devInfo.isCalibrated[1] = respResult.data[offset + 1];
                    if (respResult.data.size() > 89)
                        devInfo.isCalibrated[2] =
                            respResult.data[offset + 2];
                    else
                        devInfo.isCalibrated[2] = 0;
                }

                if (respResult.data.size() > 98)
                {
                    offset = 91;
                    uint8_t biscaled = respResult.data[offset];
                    devInfo.isCalibrated[0] = biscaled & 0x01;
                    devInfo.isCalibrated[1] = (biscaled >> 1) & 0x01;
                    devInfo.isCalibrated[2] = (biscaled >> 2) & 0x01;
                    offset += 1;

                    devInfo.udpDestIp[3] = respResult.data[offset];
                    devInfo.udpDestIp[2] = respResult.data[offset + 1];
                    devInfo.udpDestIp[1] = respResult.data[offset + 2];
                    devInfo.udpDestIp[0] = respResult.data[offset + 3];
                    devInfo.udpDestPort =
                        (respResult.data[offset + 4] << 8) |
                        respResult.data[offset + 5];
                    offset += 6;
                    devInfo.timeSyncType = respResult.data[offset];
                }

                return true;
            }
            else
            {
                std::cout << "getdevinfo datasize="
                          << std::to_string(respResult.data.size())
                          << std::endl;

                XTLOGINFO("getdevinfo datasize=" +
                          std::to_string(respResult.data.size()));

                devInfo.fwVersion = "unknow";
            }
        }

        return false;
    }

    bool XtSdk::getDevConfig(RespDevConfig &devConfig)
    {
        XTDAEMONUSING;
        RespResult respResult = xtdaemon->transceiveCmd(CMD_GET_CONFIG, {});
        if (respResult.ret_code == CmdResp_OK)
        {
            // std::cout << respResult.data.size() << " cfg " <<
            // sizeof(struct DevCfg_t) << std::endl;
            if (respResult.data.size() == sizeof(struct DevCfg_t))
            {
                struct DevCfg_t rawdevcfg;
                memcpy(&rawdevcfg, respResult.data.data(), sizeof(rawdevcfg));

                uint8_t maxfreq = 0; // 找最大的频率
                if (rawdevcfg.hdrmode != 1)
                    maxfreq = rawdevcfg.freq[0];
                else
                {
                    for (int i = 0; i < 5; i++)
                    {
                        if (rawdevcfg.integtime[i] > 0)
                        {
                            if (rawdevcfg.freq[i] > maxfreq)
                                maxfreq = rawdevcfg.freq[i];
                        }
                    }
                }
                if (maxfreq == 0)
                    devConfig.modFreq = FREQ_24M;
                else if (maxfreq == 1)
                    devConfig.modFreq = FREQ_12M;
                if (maxfreq > 1)
                    devConfig.modFreq = FREQ_6M;
                if (maxfreq > 3)
                    devConfig.modFreq = FREQ_3M;
                if (maxfreq > 7)
                    devConfig.modFreq = FREQ_1_5M;

                devConfig.endianType = rawdevcfg.endiantype;

                devConfig.imgType = IMG_AMPLITUDE;

                if (rawdevcfg.imageflags == (IMG_DIST | IMG_AMP))
                    devConfig.imgType = IMG_AMPLITUDE;
                else if (rawdevcfg.imageflags == IMG_DIST)
                    devConfig.imgType = IMG_DISTANCE;
                else if (rawdevcfg.imageflags == IMG_GS16)
                    devConfig.imgType = IMG_GRAYSCALE;

                if ((xtdaemon->needPointcloud) && (devConfig.imgType != IMG_GRAYSCALE))
                    devConfig.imgType = (ImageType)(devConfig.imgType + 3);

                devConfig.hdrMode = (HDRMode)rawdevcfg.hdrmode;
                devConfig.integrationTimes[0] = rawdevcfg.integtime[0];
                devConfig.integrationTimes[1] = rawdevcfg.integtime[1];
                devConfig.integrationTimes[2] = rawdevcfg.integtime[2];
                devConfig.integrationTimes[3] = rawdevcfg.integtime[3];
                devConfig.integrationTimes[4] = rawdevcfg.integtime[4];
                devConfig.integrationTimeGs = rawdevcfg.integtimegs;
                devConfig.freq[0] = rawdevcfg.freq[0];
                devConfig.freq[1] = rawdevcfg.freq[1];
                devConfig.freq[2] = rawdevcfg.freq[2];
                devConfig.freq[3] = rawdevcfg.freq[3];
                devConfig.freq[4] = rawdevcfg.freq[4];
                devConfig.miniAmp = rawdevcfg.miniAmp;
                devConfig.maxfps = rawdevcfg.usefps;
                devConfig.setmaxfps = rawdevcfg.setfps;
                devConfig.bBinningH = (rawdevcfg.binning & 0x2) ? 1 : 0;
                devConfig.bBinningV = (rawdevcfg.binning & 0x1) ? 1 : 0;
                devConfig.freqChannel = rawdevcfg.channel;
                devConfig.bCompensateOn = 0x3;
                devConfig.bcut_filteron = rawdevcfg.bcut_filteron;
                devConfig.cut_intgrttime0 = rawdevcfg.cutIntDist[0][0];
                devConfig.cut_distance0 = rawdevcfg.cutIntDist[0][1];
                devConfig.cut_intgrttime1 = rawdevcfg.cutIntDist[1][0];
                devConfig.cut_distance1 = rawdevcfg.cutIntDist[1][1];
                devConfig.roi[0] = rawdevcfg.roix[0];
                devConfig.roi[1] = rawdevcfg.roiy[0];
                devConfig.roi[2] = rawdevcfg.roix[1];
                devConfig.roi[3] = rawdevcfg.roiy[1];
                devConfig.vcsel = rawdevcfg.vcsel;
                devConfig.ptpdomain = rawdevcfg.ptpdomain;

                devConfig.version = 3;

                return true;
            }
            else
            {
                if (respResult.data.size() > 13)
                {
                    devConfig.imgType = (ImageType)respResult.data[0];
                    if ((xtdaemon->needPointcloud) &&
                        (devConfig.imgType != IMG_GRAYSCALE))
                    {
                        devConfig.imgType = (ImageType)(respResult.data[0] + 3);
                    }
                    devConfig.modFreq = (ModulationFreq)respResult.data[1];
                    devConfig.hdrMode = (HDRMode)respResult.data[2];
                    devConfig.miniAmp = (uint16_t)respResult.data[3] << 8 |
                                        (uint16_t)respResult.data[4];
                    devConfig.integrationTimes[0] =
                        (uint16_t)respResult.data[5] << 8 |
                        (uint16_t)respResult.data[6];
                    devConfig.integrationTimes[1] =
                        (uint16_t)respResult.data[7] << 8 |
                        (uint16_t)respResult.data[8];
                    devConfig.integrationTimes[2] =
                        (uint16_t)respResult.data[9] << 8 |
                        (uint16_t)respResult.data[10];
                    devConfig.integrationTimeGs = (uint16_t)respResult.data[11]
                                                      << 8 |
                                                  (uint16_t)respResult.data[12];
                    devConfig.isFilterOn = respResult.data[13];

                    devConfig.integrationTimes[3] = 0;
                    devConfig.integrationTimes[4] = 0;

                    devConfig.version = 2;

                    devConfig.bBinningH = 0;
                    devConfig.bBinningV = 0;

                    if (respResult.data.size() > 25)
                    {
                        devConfig.roi[0] = (uint16_t)respResult.data[14] << 8 |
                                           (uint16_t)respResult.data[15];
                        devConfig.roi[1] = (uint16_t)respResult.data[16] << 8 |
                                           (uint16_t)respResult.data[17];
                        devConfig.roi[2] = (uint16_t)respResult.data[18] << 8 |
                                           (uint16_t)respResult.data[19];
                        devConfig.roi[3] = (uint16_t)respResult.data[20] << 8 |
                                           (uint16_t)respResult.data[21];

                        devConfig.maxfps = respResult.data[22];
                        devConfig.bCompensateOn = respResult.data[23];
                        devConfig.bBinningH = (respResult.data[24] >> 4) & 0x0f;
                        devConfig.bBinningV = respResult.data[24] & 0x0f;
                    }
                    if (respResult.data.size() > 27)
                    {
                        devConfig.setmaxfps = respResult.data[25];
                        devConfig.freqChannel = respResult.data[26];
                    }
                    else
                    {
                        devConfig.setmaxfps = 0;
                        devConfig.freqChannel = 0;
                    }
                    return true;
                }
            }
        }

        return false;
    }

    bool XtSdk::setIp(std::string ip, std::string mask, std::string gate)
    {
        XTDAEMONUSING;
        XTLOGINFO(ip + " " + mask + " " + gate);
        uint8_t data[13];
        if ((Utils::ipIsValid(ip) == false) ||
            (Utils::ipIsValid(mask) == false) ||
            (Utils::ipIsValid(gate) == false))
            return false;

        Utils::ipstr_parse(ip.c_str(), data);
        Utils::ipstr_parse(mask.c_str(), &(data[4]));
        Utils::ipstr_parse(gate.c_str(), &(data[8]));

        XByteArray xdata = {};
        xdata.assign(&(data[0]), &(data[12]));
        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_IP, xdata);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::resetDev()
    {
        XTDAEMONUSING;
        XTLOGINFO("");
        RespResult respResult =
            xtdaemon->transceiveCmd(CMD_RESET, {'X', 'I', 'N', 'T', 'A', 'N'});

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setFilter(uint16_t temporal_factor, uint16_t temporal_threshold,
                          uint16_t edgefilter_threshold)
    {
        return false;
    }

    bool XtSdk::setIntTimesus(uint16_t timeGs, uint16_t time1, uint16_t time2,
                              uint16_t time3, uint16_t time4, uint16_t time5)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(time1) + " " + std::to_string(time2) + " " +
                  std::to_string(time3) + " " + std::to_string(timeGs) + " " +
                  (time4 > 0 ? std::to_string(time4) : "") +
                  (time5 > 0 ? std::to_string(time5) : ""));

        XByteArray data;
        data.resize(12);

        Utils::setValueUint16Endian(&data[0], time1,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[2], time2,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[4], time3,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[6], timeGs,
                                    xtdaemon->get_endianType());

        Utils::setValueUint16Endian(&data[8], time4,
                                    xtdaemon->get_endianType());

        Utils::setValueUint16Endian(&data[10], time5,
                                    xtdaemon->get_endianType());

        RespResult respResult =
            xtdaemon->transceiveCmd(CMD_SET_INTEGRATIONTIME, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setMinAmplitude(uint16_t minAmplitude)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(minAmplitude));

        XByteArray data;
        data.resize(2);
        Utils::setValueUint16Endian(&data[0], minAmplitude,
                                    xtdaemon->get_endianType());

        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_MINAMP, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setHdrMode(HDRMode mode)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(mode));
        uint8_t hdrtype = 0;
        if (mode == HDR_TAMPORAL)
            hdrtype = 2;

        if (mode == HDR_SPATIAL)
            hdrtype = 1;

        XByteArray data = {hdrtype};
        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_HDR, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setModFreq(ModulationFreq freqType)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(freqType));
        uint8_t freqindex = freqType;
        if (freqType > FREQ_0_75M)
            freqindex = FREQ_0_75M;

        XByteArray data = {freqindex};

        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_MODFREQ, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
        {
            XTLOGWRN(std::to_string(freqType) + "failed");
            return false;
        }
    }

    uint8_t getFreq(ModulationFreq freqType)
    {
        uint8_t freq = 0;

        switch (freqType)
        {
        case ModulationFreq::FREQ_24M:
            freq = 0;
            break;
        case ModulationFreq::FREQ_12M:
            freq = 1;
            break;
        case ModulationFreq::FREQ_6M:
            freq = 3;
            break;
        case ModulationFreq::FREQ_4_8M:
            freq = 4;
            break;
        case ModulationFreq::FREQ_3M:
            freq = 7;
            break;
        case ModulationFreq::FREQ_1_5M:
            freq = 15;
            break;
        case ModulationFreq::FREQ_0_75M:
            freq = 31;
            break;

        default:
            std::cout << "Unknown freq!" << std::endl;
            break;
        }
        return freq;
    }

    bool XtSdk::setMultiModFreq(ModulationFreq freqType1,
                                ModulationFreq freqType2,
                                ModulationFreq freqType3,
                                ModulationFreq freqType4,
                                ModulationFreq freqType5)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(freqType1) + " " + std::to_string(freqType2) +
                  " " + std::to_string(freqType3));
        uint8_t freq[4];

        freq[0] = getFreq(freqType1);
        freq[1] = getFreq(freqType2);
        freq[2] = getFreq(freqType3);
        freq[3] = getFreq(freqType4);
        freq[4] = getFreq(freqType5);

        XByteArray data = {freq[0], freq[1], freq[2], freq[3], freq[4]};
        XByteArray respdata;

        if (customCmd(27, data, respdata))
            return true;
        else
        {
            XTLOGWRN("setMultiModFreq failed");
            return false;
        }
    }

    bool XtSdk::setAdditionalGray(const uint8_t &on)
    {
        XTDAEMONUSING;
        XByteArray data = {0x22, on};
        XByteArray respdata;
        if (customCmd(202, data, respdata))
            return true;
        else
        {
            XTLOGWRN("setAdditionalGray failed");
            return false;
        }
    }

    bool XtSdk::restoreDefault()
    {
        XTDAEMONUSING;
        RespResult respResult = xtdaemon->transceiveCmd(
            CMD_RESTORE_DEFAULT, {'X', 'I', 'N', 'T', 'A', 'N'});

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setRoi(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(x0) + " " + std::to_string(y0));
        if ((x0 > 319) || (y0 > 239) || (x1 > 319) || (y1 > 239) || (x1 < x0) ||
            (y1 < y0))
        {
            return false;
        }
        XByteArray data;
        data.resize(10);

        Utils::setValueUint16Endian(&data[0], x0,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[2], y0,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[4], x1,
                                    xtdaemon->get_endianType());
        Utils::setValueUint16Endian(&data[6], y1,
                                    xtdaemon->get_endianType());

        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_ROI, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setMaxFps(uint8_t maxfps)
    {
        XTDAEMONUSING;
        XTLOGINFO(std::to_string(maxfps));

        XByteArray data = {maxfps};
        RespResult respResult = xtdaemon->transceiveCmd(CMD_SET_MAXFPS, data);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::setUdpDestIp(std::string ip, uint16_t port)
    {
        XTDAEMONUSING;
        if (isconnect() == false)
        {
            XTLOGINFO("noconnect: " + ip + " " + std::to_string(port));
            if (port < 10000)
                xtdaemon->udpPort = port;

            if (Utils::ipIsValid(ip) == true)
                xtdaemon->hostIpStr = ip;

            return false;
        }
        XTLOGINFO(ip + " " + std::to_string(port));

        uint8_t data[7];
        if (Utils::ipIsValid(ip) == false)
            return false;

        Utils::ipstr_parse(ip.c_str(), data);

        Utils::setValueUint16Endian(&data[4], port,
                                    xtdaemon->get_endianType());

        XByteArray xdata = {};
        xdata.assign(&(data[0]), &(data[6]));
        RespResult respResult =
            xtdaemon->transceiveCmd(CMD_SET_UDPIP, xdata);

        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    bool XtSdk::customCmd(uint8_t cmdId, XByteArray data, XByteArray &respData,
                          uint32_t timeoutms)
    {
        XTDAEMONUSING;
        if ((cmdId != 0) && (cmdId != 49))
            XTLOGINFO(std::to_string(cmdId) + " " +
                      std::to_string(data.size()));
        RespResult respResult = xtdaemon->transceiveCmd(cmdId, data, timeoutms);

        respData = respResult.data;
        if (respResult.ret_code == CmdResp_OK)
            return true;
        else
            return false;
    }

    int XtSdk::getfps()
    {
        XTDAEMONUSING;
        return xtdaemon->fps;
    }

    int XtSdk::getimufps()
    {
        XTDAEMONUSING;
        return xtdaemon->imufps;
    }

    bool XtSdk::doUdpFrameData(const std::vector<uint8_t> &udpframeData, std::string frame_label)
    {
        XTDAEMONUSING;
        return xtdaemon->doUdpFrameData(udpframeData, frame_label);
    }

    uint8_t XtSdk::getEndianType()
    {
        XTDAEMONUSING;
        return xtdaemon->get_endianType();
    }

    std::string &XtSdk::getLogtagName()
    {
        return logtagname;
    }

    bool camparamsChanged(CamParameterS &camparams, CamParameterS &camparams_last)
    {
        if ((camparams.cx == camparams_last.cx) && (camparams.cy == camparams_last.cy) && (camparams.cx == camparams_last.cx) && (camparams.fx == camparams_last.fx) && (camparams.fy == camparams_last.fy) && (camparams.p1 == camparams_last.p1) && (camparams.p2 == camparams_last.p2) && (camparams.k1 == camparams_last.k1) && (camparams.k2 == camparams_last.k2) && (camparams.k3 == camparams_last.k3))
            return false;
        return true;
    }

    void XtSdk::setPlayState(bool is_playing)
    {
        XTDAEMONUSING;
        xtdaemon->is_playing = is_playing;
    }

    bool XtSdk::getPlayState()
    {
        XTDAEMONUSING;
        return xtdaemon->is_playing;
    }

    bool XtSdk::setBinningV(uint8_t flag)
    {
        XByteArray data = {flag};
        XByteArray respData;
        if (customCmd(12, data, respData))
            return true;
        else
            return false;
    }

    bool XtSdk::doXbinFrameData(const std::string &xbin_path)
    {
        XTDAEMONUSING;
        if (!Utils::hasExtension(xbin_path, ".xbin"))
        {
            std::cout << "read xbin file failed: " << xbin_path << std::endl;
            return false;
        }

        uint32_t xbinVer = 0;
        CamParameterS camparams;
        RecordCfgINFO_V2 recordCfgInfo;

        std::cout << "read xbin file: " << xbin_path << std::endl;

        std::ifstream infs(xbin_path, std::ios::binary);

        infs.read((char *)(&xbinVer), sizeof(xbinVer));

        // infs.read((char *)(&recordCfgInfo), sizeof(recordCfgInfo));
        std::vector<uint8_t> fileData;
        std::vector<uint8_t> frameData;
        uint8_t value;
        int framedatapos = 0;
        while (infs.read(reinterpret_cast<char *>(&value), sizeof(value)))
        {
            fileData.push_back(value);
        }

        // while (!infs.eof())
        // {
        //     uint8_t value;
        //     infs.read((char *)(&value), sizeof(value));
        //     fileData.push_back(value);
        // }

        infs.close();
        uint32_t endmark = Utils::getValueUint32Endian(&(fileData[fileData.size() - 4]), Endian_Big);
        if (endmark == 0xFF7E55AA) // 去尾
            fileData.erase(fileData.end() - 4, fileData.end());

        if (xbinVer == 2)
        {
            memcpy(&camparams, &fileData[0], sizeof(camparams));
            framedatapos += sizeof(camparams);
            memcpy(&recordCfgInfo, &fileData[framedatapos], sizeof(recordCfgInfo));
            framedatapos += sizeof(recordCfgInfo);
            frameData.assign(fileData.begin() + framedatapos, fileData.end());
            if (camparamsChanged(camparams, camparams_playlast))
            {
                camparams_playlast = camparams;
                xtdaemon->cartesianTransform->maptable(camparams);
            }

            if (frameData[frameData.size() - 1] == 3) // v2格式下录制的v3数据
            {
                uint16_t datainfosize = Utils::getValueUint16Endian(&(frameData[frameData.size() - 4]), Endian_Little);

                if ((datainfosize > 200) && (datainfosize < 250))
                {
                    struct FrameInfoOld_t frameinfo_old;
                    std::vector<uint8_t> tempdata;
                    tempdata.resize(sizeof(frameinfo_old));
                    memset(&frameinfo_old, 0, sizeof(frameinfo_old));

                    uint16_t width = Utils::getValueUint16Endian(&(frameData[4]), Endian_Big);
                    uint16_t height = Utils::getValueUint16Endian(&(frameData[6]), Endian_Big);
                    if (width < 161)
                        frameinfo_old.binning |= 0x10;
                    if (width < 121)
                        frameinfo_old.binning |= 0x01;

                    frameinfo_old.version = 2;

                    memcpy(&tempdata[0], &frameinfo_old, sizeof(frameinfo_old));
                    frameData.erase(frameData.end() - datainfosize, frameData.end());
                    frameData.insert(frameData.end(), tempdata.begin(), tempdata.end());
                }
                else
                {
                    std::cout << "size error: " + std::to_string(datainfosize) << "  " + std::to_string(sizeof(FrameInfoOld_t)) << std::endl;
                    return false;
                }
            }
        }
        else if (xbinVer == 3)
        {
            // 去头
            uint32_t startmark = Utils::getValueUint32Endian(&(fileData[0]), Endian_Big);
            if (startmark == 0x7EFFAA55)
                fileData.erase(fileData.begin(), fileData.begin() + 8);
            frameData.assign(fileData.begin(), fileData.end());

            struct FrameInfo_t frameinfo;
            uint16_t datainfosize = Utils::getValueUint16Endian(&(frameData[frameData.size() - 4]), Endian_Little);

            if (datainfosize != sizeof(frameinfo))
            {
                std::cout << "size error: " + std::to_string(datainfosize) << "  " + std::to_string(sizeof(frameinfo)) << std::endl;
                return false;
            }

            memcpy(&frameinfo, &frameData[frameData.size() - datainfosize], datainfosize);
            memcpy(&camparams, frameinfo.lensparameters, sizeof(camparams));
        }

        else
        {
            std::cout << "fileVer not support" << std::endl;
            return false;
        }
        xtdaemon->doDataFrame(frameData);
        return true;
    }

    bool XtSdk::doXbinRecord(const std::string &xbin_path, const std::shared_ptr<Frame> &frame,
                             const uint32_t &currIndex)
    {
        // uint8_t i = 0;
        // CamParameterS g_camparams;
        // if (currIndex == 0) {
        //     std::string record_dir = xbin_path + "/" + Utils::getTimeStr();
        //     while (i <= 8 && !getLensCalidata(g_camparams)) {
        //         std::this_thread::sleep_for(std::chrono::milliseconds(50));
        //         i++;
        //     }
        //     if (i > 8) {
        //         std::cout << "ERROR: get lens calib data failed" << std::endl;
        //         return false;
        //     }
        //     ::create_directory(record_dir);
        // }

        uint32_t xbinVer = 2;

        std::string filename = xbin_path + "/" + std::to_string(currIndex) + ".xbin";

        // 打开文件
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            std::cout << "Failed to open file for writing." << std::endl;
            return false;
        }

        // 确定数据大小和起始位置
        int datasize = frame->frameData.size();
        int framestartpos = 0;

        if (frame->frame_version > 2)
        { // V3 版本不存配置信息
            xbinVer = 3;
            file.write(reinterpret_cast<const char *>(&xbinVer), sizeof(xbinVer));
        }
        else
        {
            // file.write(reinterpret_cast<const char *>(&xbinVer), sizeof(xbinVer));
            // file.write(reinterpret_cast<const char *>(&g_camparams), sizeof(g_camparams));
            // file.write(reinterpret_cast<const char *>(&g_recordCfgInfo),
            // sizeof(g_recordCfgInfo));

            // uint32_t startmark = Utils::getValueUint32Endian(&(frame->frameData[0]), Endian_Big);
            // if (startmark == 0x7EFFAA55) // v2 不录头
            //     framestartpos += 8;

            std::cout << "old version frame not supported." << std::endl;
            return false;
        }

        // 写入数据
        for (int i = framestartpos; i < datasize; ++i)
        {
            file.put(frame->frameData[i]);
        }

        // 关闭文件
        file.close();
        return true;
    }

    void XtSdk::eventInternalProcess(std::shared_ptr<CBEventData> eventdata,
                                     void *psdk)
    {
        XtSdk *xtsdk = (XtSdk *)psdk;
        XtDaemon *xtdaemon = (XtDaemon *)xtsdk->pInteranl;
        std::string &logtagname = xtsdk->getLogtagName();
        XTLOGINFO(std::to_string(eventdata->cmdid));
        if (xtsdk->isconnect() &&
            (eventdata->cmdid == 0xfe)) // 端口打开后第一次连接上设备
        {
            RespDevInfo devinfo;
            xtsdk->getDevInfo(devinfo);

            transform(devinfo.sn.begin(), devinfo.sn.end(), devinfo.sn.begin(), ::tolower);
            transform(devinfo.fwVersion.begin(), devinfo.fwVersion.end(), devinfo.fwVersion.begin(), ::tolower);

            XTLOGINFO("sn =" + devinfo.sn);
            XTLOGINFO("fw =" + devinfo.fwVersion + "boot = " + devinfo.bootVersion);

            int fwlen = devinfo.fwVersion.length();
            int vpos = devinfo.fwVersion.find('v');

            if (fwlen > 8)
            {
                std::string verfstr = devinfo.fwVersion.substr(vpos + 1, fwlen - 2);
                double fwversionf = atof(verfstr.c_str());
                std::cout << "fw release version=" << fwversionf << "  str=" << verfstr << std::endl; // Major
            }

            CamParameterS camparams;
            if (xtsdk->getLensCalidata(camparams))
            {
                xtdaemon->cartesianTransform->maptable(camparams);

                std::string logstr = "getLensCalidata=true";
                logstr += " cx=" + std::to_string(camparams.cx);
                logstr += " cy=" + std::to_string(camparams.cy);
                logstr += " fx=" + std::to_string(camparams.fx);
                logstr += " fy=" + std::to_string(camparams.fy);
                logstr += " k1=" + std::to_string(camparams.k1);
                logstr += " k2=" + std::to_string(camparams.k2);
                logstr += " k3=" + std::to_string(camparams.k3);
                logstr += " p1=" + std::to_string(camparams.p1);
                logstr += " p2=" + std::to_string(camparams.p2);

                XTLOGINFO(logstr);
            }
            else
            {
                XTLOGINFO("getLensCalidata failed, use default params");
                std::cout << "getLensCalidata failed, use default params"
                          << std::endl;

                camparams = {(float)170.0, (float)170.0,
                             (float)160.0, (float)120.0,
                             (float)0.0, (float)0.0,
                             (float)0.0000000000, (float)0.0,
                             (float)0.0};
                xtdaemon->cartesianTransform->maptable(camparams);
            }
            if (xtsdk->isUsedNet())
            {
                if (Utils::ipIsValid(xtdaemon->hostIpStr) ||
                    (xtdaemon->udpPort != 7687))
                {
                    if (Utils::ipIsValid(xtdaemon->hostIpStr))
                        xtsdk->setUdpDestIp(xtdaemon->hostIpStr,
                                            xtdaemon->udpPort);
                    else
                        xtsdk->setUdpDestIp(Utils::getHostIp(),
                                            xtdaemon->udpPort);
                }
            }
        }
    }

} // end namespace XinTan
