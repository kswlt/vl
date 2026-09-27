/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once

#include <functional>
#include <string>
#include "frame.h"

namespace XinTan
{
    // extern void xtdlllogger(const char* pfunction, const char* plogstr);
    extern void xtapplogger(const std::string &function, const std::string &logstr);
#define XTAPPLOG(logstr) xtapplogger(__FUNCTION__, logstr)
    /**************************** 数据结构 ******************************/
    // sdk 状态
    enum SdkState
    {
        STATE_UNSTARTUP = 0,
        STATE_PORTOPENING,
        STATE_TXRX_VERIFYING,
        STATE_UDPIMGOK,
        STATE_CONNECTED,
        STATE_UNKNOW
    };

    enum ClOUDCOORD_TYPE
    {
        ClOUDCOORD_CAMERA = 0x00,
        ClOUDCOORD_CAR = 0x01
    };

    // 设备状态码
    enum DevStateCode
    {
        DevSTATE_DISCONNECTED = 0x00,
        DevSTATE_INIT = 0x01,
        DevSTATE_IDLE = 0x02,
        DevSTATE_STREAM = 0x03,
        DevSTATE_ERR_CSI = 0x04,
        DevSTATE_ERR_I2C = 0x05,
        DevSTATE_ERR_TEMPL = 0x06,
        DevSTATE_WARN_TEMP = 0x07,
        DevSTATE_TEMP_DownFreq = 0x08,
        DevSTATE_ERR_TEMPH = 0x09,
        DevSTATE_ERR_NOSENSOR = 0x0A,
        DevSTATE_ERR_NOMODIC = 0x0B,
        DevSTATE_ERR_NOTEMPIC = 0x0C,
        DevSTATE_ERR_BTNOAPP = 0x0D,
        DevSTATE_BOOTLOADER = 0x0E,
        DevSTATE_ERR_UNKNOW = 0x0F,
        DevSTATE_ERR_POWERV = 0x10,
        DevSTATE_ERR_POWERA = 0x11,
        DevSTATE_ERR_RAM = 0x12,
        DevSTATE_ERR_FLASH = 0x13,
        DevSTATE_ERR_NOCALI = 0x14,
        DevSTATE_ERR_NOIPARMS = 0x15,
        DevSTATE_ERR_NOMAC = 0x16,
        DevSTATE_ERR_NOSN = 0x17,
        DevSTATE_ERR_TEMPPARMS = 0x18,
        DevSTATE_CALIBRATING = 0x19,
        DevSTATE_ERR_NOIMUPARAM = 0x1A,
        DevSTATE_ERR_MAX = 0x1B
    };

    // 命令返回状态码
    enum CmdRespCode
    {
        CmdResp_OK = 0x00,
        CmdResp_UNSUPPORT = 0x01,
        CmdResp_BUSY = 0x02,
        CmdResp_REJECT = 0x03,
        CmdResp_REPORT = 0x07,
        CmdResp_ERR_FORMAT = 0x08,
        CmdResp_ERR_DATA = 0x09,
        CmdResp_CSI = 0x0A,
        CmdResp_I2C = 0x0B,
        CmdResp_TEMPH = 0x0C,
        CmdResp_TEMPL = 0x0D,
        CmdResp_ERR_UNKNOW = 0x0E,
        CmdResp_TIMEOUT = 0x0F
    };

    const uint8_t REPORT_LOG = 209;

    // 需要输出的图像类型
    enum ImageType
    {
        IMG_DISTANCE,
        IMG_AMPLITUDE,
        IMG_GRAYSCALE,
        IMG_POINTCLOUD,
        IMG_POINTCLOUDAMP
    };
    const std::vector<std::string> ImageTypeStr = {"IMG_DISTANCE",
                                                   "IMG_AMPLITUDE",
                                                   "IMG_GRAYSCALE",
                                                   "IMG_POINTCLOUD",
                                                   "IMG_POINTCLOUDAMP"};

    // 可以配置的调整频率类型
    enum ModulationFreq
    {
        FREQ_12M = 0x00,
        FREQ_6M = 0x01,
        FREQ_24M = 0x02,
        FREQ_3M = 0x03,
        FREQ_1_5M = 0x04,
        FREQ_0_75M = 0x05,
        FREQ_4_8M = 0x06
    };

    const std::vector<std::string> ModulationFreqStr = {"FREQ_12M",
                                                        "FREQ_6M",
                                                        "FREQ_24M",
                                                        "FREQ_3M",
                                                        "FREQ_1_5M",
                                                        "FREQ_0_75M",
                                                        "FREQ_4_8M"};

    // 可以配置的HDR类型
    enum HDRMode
    {
        HDR_OFF,
        HDR_TAMPORAL,
        HDR_SPATIAL
    };

    const std::vector<std::string> HDRModeStr = {"HDR_OFF",
                                                 "HDR_TAMPORAL",
                                                 "HDR_SPATIAL"};

    // 获取的设备信息
    struct RespDevInfo
    {
        uint8_t ip[12];
        uint8_t mac[6];
        std::string fwVersion;
        std::string sn;
        std::string bootVersion;
        uint16_t chipid[2];
        std::string chipidStr;
        uint8_t isCalibrated[3];
        uint8_t udpDestIp[4];
        uint16_t udpDestPort;
        uint8_t timeSyncType;
        uint8_t bdrnulens;
    };

    // 获取的设备配置信息
    struct RespDevConfig
    {
        ImageType imgType;
        ModulationFreq modFreq;
        HDRMode hdrMode;
        uint16_t integrationTimes[5];
        uint16_t integrationTimeGs;
        uint16_t miniAmp;
        uint8_t isFilterOn;
        uint16_t roi[4];
        uint8_t maxfps;
        uint8_t bCompensateOn;
        uint8_t bBinningH;
        uint8_t bBinningV;
        uint8_t freqChannel;
        uint8_t setmaxfps;
        uint8_t vcsel;
        uint8_t ptpdomain;
        uint8_t endianType;
        uint8_t version;

        uint8_t freq[5];
        uint8_t bcut_filteron;
        uint16_t cut_intgrttime0;
        uint16_t cut_distance0;
        uint16_t cut_intgrttime1;
        uint16_t cut_distance1;
    };

    // 上报的Event 数据结构
    struct CBEventData
    {
        std::string eventstr; // event类型字符串， "portOpened":当通讯端口打开是上报   "connect":当第一次命令交互成功上报
        uint8_t cmdid;        // 从设备上报的命令id
        uint8_t cmdstate;     // 从设备上报的命令状态
        XByteArray data;      // 从设备上报的数据

        CBEventData(std::string _eventstr, uint8_t _cmdid, XByteArray _data, uint8_t _cmdstate) : eventstr(_eventstr),
                                                                                                  cmdid(_cmdid),
                                                                                                  cmdstate(_cmdstate),
                                                                                                  data(_data)
        {
        }
    };

    // sdk 内部点云元素结构, 定义在frame.h文件
    /*struct XtPointXYZI
    {
        float x;
        float y;
        float z;
        float intensity;
    };*/

    /**************************** xtsdk 接口类定义  ******************************/
    class XtSdk
    {
    public:
        typedef std::shared_ptr<XtSdk> Ptr;
        typedef std::shared_ptr<const XtSdk> ConstPtr;
        // logpath 可以指定日志路径，logtag 可以指定sdk实例的日志标记，pxt 无需配置
        XtSdk(const std::string logpath = "./xtlog/", const std::string logtag = "", void *pxt = nullptr);
        ~XtSdk();
        uint8_t getEndianType();
        /***********  SDK 必要设置 **********************/
        void setCallback(std::function<void(const std::shared_ptr<CBEventData> &)> eventcallback = nullptr,
                         std::function<void(const std::shared_ptr<Frame> &)> imgcallback = nullptr);

        void setCallback(std::function<void(const std::shared_ptr<CBEventData> &, void *)> eventcallback,
                         std::function<void(const std::shared_ptr<Frame> &, void *)> imgcallback, void *peventIn = nullptr, void *pimgIn = nullptr);

        bool setConnectIpaddress(std::string ipAddress);           // 如果用网络连接，使用这个API设置要连接设备的ip地址(如 "192.168.0.101")
        bool setConnectSerialportName(std::string serialportName); // 如果用USB连接，使用这个API设置要连接设备的COM口地址(如 "COM2")

        bool isUsedNet(); // 设备连接是否通过网络
        bool set_endianCheck(bool enable);

        /***********  SDK 运行相关 **********************/
        void startup();  // 启动sdk运行
        void shutdown(); // 端口设备连接

        bool isconnect(); // 设备连接是否成功

        SdkState getSdkState();     // 获取SDK状态
        DevStateCode getDevState(); // 获取设备状态
        std::string getStateStr();  // 获取SDK 状态字符串

        int getfps(); // 获取SDK计算的帧率
        int getimufps();

        // sdk中支持的滤波，算法运行在主机上
        void setPostProcess(const float &dilation_pixels, const uint8_t &mode, uint8_t winsize = 9, uint8_t motion_size = 5);
        bool setSdkKalmanFilter(uint16_t factor, uint16_t threshold, uint16_t timedf = 300); // 设置sdk中的卡尔曼滤波
        bool setSdkEdgeFilter(uint16_t threshold);                                           // 设置sdk中的飞点滤波
        bool setSdkMedianFilter(uint16_t size);                                              // 设置sdk中的中值滤波
        bool setSdkAvgFilter(uint16_t size, uint16_t timedf = 300);
        void resetFilters();

        // 灰尘滤波. threshold:帧间比较时用的距离差阈值(最大1000mm)， framecount：使用几帧做这个滤波(范围2~9)  timedf：两帧见容许的时间差
        bool setSdkDustFilter(uint16_t threshold, uint16_t framecount = 4, uint16_t timedf = 300, uint16_t validpercent = 100);
        bool setSdkReflectiveFilter(const float &threshold_min, const float &threshold_max);
        bool setSpatialFilter(const float &alpha, const uint32_t &delta, const uint8_t &iterations);

        bool clearAllSdkFilter(); // 清除SDK中所有的滤波设置

        bool doUdpFrameData(const std::vector<uint8_t> &udpframeData, std::string frame_label = "");

        bool doXbinFrameData(const std::string &xbin_path);

        bool doXbinRecord(const std::string &xbin_path, const std::shared_ptr<Frame> &frame,
                          const uint32_t &currIndex);

        bool updateFW(const std::string &bin_path);

        void setTransMirror(bool hmirror, bool vmirror); // hmirror  : 水平翻转, vmirror  : 垂直翻转

        void setPointsCornerCut(bool iscut); // iscut  : 是否对点云进行剪切

        void setPointsLsbCut(uint16_t minAmp); // 设置过滤的最小信号强度

        void setDownSample(uint16_t x, uint16_t y); // 设置下采样合并的点数

        void setCutCorner(uint32_t cutvalue); // 设置四角切除的

        void setReflectivityCoef(float coef); // 设置计算发射率的系数
        void setSdkCutMaxRefFilter(uint32_t cutmaxref);

        void setPlayState(bool is_playing);

        bool getPlayState();

        /***********  命令相关 API **********************/
        bool testDev(); // 测试设备命令交互是否通

        bool resetDev();       // 重启设备
        bool restoreDefault(); // 参数恢复出厂设置

        bool start(ImageType imgType, bool isOnce = false); // 指定期望的图像类型 进行测量, isOnce 是否单次获取

        bool stop(); // 让设备停止测量

        bool getDevInfo(RespDevInfo &devInfo); // 获取设备信息

        bool getDevConfig(RespDevConfig &devConfig); // 获取设备设置信息

        bool setIp(std::string ip, std::string mask, std::string gate); // 设置设备的ip地址相关

        bool setUdpDestIp(std::string ip, uint16_t port = 7687); // 设置UDP目标IP地址, 可以sdk实例化后只能调用一次进行初始化，是配置udp端口的用法如 setUdpDestIp("", 9001);

        bool setFilter(uint16_t temporal_factor, uint16_t temporal_threshold, uint16_t edgefilter_threshold); // 已经丢弃不用，是空接口

        bool setIntTimesus(uint16_t timeGs, uint16_t time1, uint16_t time2, uint16_t time3, uint16_t time4 = 0, uint16_t time5 = 0); // 设置积分时间

        bool setMinAmplitude(uint16_t minAmplitude); // 设置有效的信号幅度下限

        bool setHdrMode(HDRMode mode); // 设置HDR 类型

        bool setModFreq(ModulationFreq freqType); // 设置设备调整频率

        bool setMultiModFreq(ModulationFreq freqType1, ModulationFreq freqType2, ModulationFreq freqType3, ModulationFreq freqType4 = FREQ_24M, ModulationFreq freqType5 = FREQ_24M); // 设置设备调整频率

        bool setRoi(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1); // 设置ROI, y上下对称，上面去多少行，下面也去多少行

        bool setMaxFps(uint8_t maxfps); // 设置设备测量的最快帧率，(HDR模式配置为实际要的帧率的多倍(几个积分时间))

        bool setSdkCloudCoordType(ClOUDCOORD_TYPE type); // 设置sdk中输出点云的坐标系

        bool setAdditionalGray(const uint8_t &on);

        bool getLensCalidata(CamParameterS &camparameter); // 获取镜头内参

        bool getImuExtParamters(ExtrinsicIMULidar &imuparameters, uint8_t flag = 1); // 获取IMU外参数

        bool customCmd(uint8_t cmdId, XByteArray data, XByteArray &respData, uint32_t timeoutms = 1000); // 自组织命令和数据和设备交互

        bool setBinningV(uint8_t flag);
        std::string &getLogtagName(); // 获取日志标志

    private:
        std::string logtagname; // sdk内部使用, 请勿使用
        void *pInteranl;        // sdk内部使用, 请勿使用
        static void eventInternalProcess(std::shared_ptr<CBEventData> eventdata, void *psdk);
        void printVersionBanner();
        CamParameterS camparams_playlast;
    };

} // end namespace xintan
