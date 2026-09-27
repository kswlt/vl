/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "xtsdk.h"
#include "communication.h"
#include "communicationNet.h"
#include "communicationUsb.h"
#include "cartesianTransform.h"
#include "basefilter.h"
#include "tic_toc.h"
#include "utils.h"

namespace XinTan {


struct RespResult
{
    CmdRespCode ret_code;
    XByteArray data; //从设备上报的数据
};

class XtDaemon {
public:
    XtDaemon(std::string & logtag);
	~XtDaemon();

	bool setConnectAddress(std::string connectAddress);

    void setEvtInternalProcCallbk(std::function<void(const std::shared_ptr<CBEventData> &, void *) >  eventcallback, void * psdk);
    void setCallback(std::function<void(const std::shared_ptr<CBEventData> &) >  eventcallback,
					 std::function<void(const std::shared_ptr<Frame> &) >  imgcallback);
    void setCallback(std::function<void(const std::shared_ptr<CBEventData> &, void *) >  eventcallback,
                     std::function<void(const std::shared_ptr<Frame> &, void *) >  imgcallback,void * peventIn = nullptr, void * pimgIn = nullptr);



	void startup();
	void shutdown();


    RespResult transceiveCmd(uint8_t cmdId, XByteArray data, uint32_t timeoutms=300);
    bool transmitCmd(uint8_t cmdId, XByteArray data);

    void updateSdkState(SdkState state);
    void updateDevState(DevStateCode state);
    void doDataFrame(const std::vector<uint8_t> & frameData, std::string frame_label = "");
    bool doUdpFrameData (const std::vector<uint8_t> &udpframeData, std::string frame_label = "");

    void reportEvent(std::string eventstr, uint8_t cmdid, XByteArray data, uint8_t cmdstate=0);
    void reportImage(const std::shared_ptr<Frame> & frame);

    void EventCallback(const std::shared_ptr<CBEventData> & eventdata);
    void ImageCallback(const std::shared_ptr<Frame> & frame);

    void set_multicast(bool enable);

    void set_endianCheck(bool enable);
    void set_endianType (uint8_t endian);
    uint8_t get_endianType ();
    std::string & logtagname;
    CartesianTransform * cartesianTransform;
    BaseFilter * baseFilter;

    int rxedPackageN;
    int checkLiveCount;
    bool needPointcloud;

    bool isCalibrating;

	bool isSelNet;

    SdkState sdkState;
    DevStateCode devState;
    uint8_t stateCode;

    int continueNoRespCount;
    int fpscounter;
    int fps;    
    int imufpscounter;
    int imufps;
    uint16_t udpPort;
    std::string hostIpStr;

    int cmd0_checkcount;
    std::atomic<bool> bneedcheckendian;
    bool checkendian_enable;

    const std::string *devStateStr;
    bool is_playing;

private:

    std::function<void(const std::shared_ptr<CBEventData> & )> event_callback;
    std::function<void(const std::shared_ptr<Frame> & )> img_callback;

    std::function<void(const std::shared_ptr<CBEventData> & , void *)> eventIn_callback;
    std::function<void(const std::shared_ptr<CBEventData> & , void *)> internalEvent_callback;
    std::function<void(const std::shared_ptr<Frame> & , void *)> imgIn_callback;


	boost::asio::io_service ioService;

	static void DaemonFunc(XtDaemon * xtdaemon);
	static void ReceiveThreadFunc(XtDaemon * xtdaemon);
	static void UdpReceiveThreadFunc(XtDaemon * xtdaemon);
    static void rawFrameQThreadFunc(XtDaemon * xtdaemon);
    bool ParseFrame (XByteArray &frameData, std::string frame_label = "");

    std::thread * threadDaemon;
	std::thread * threadReceiver;
    std::thread * threadUdpReceiver;
    std::thread * threadrawFrameProcess;

	bool bDaemonStarted;
	bool bDaemonRuning;

	bool threadDaemonRuning;
	bool threadReceiveRuning;
	bool threadUdpReceiveRuning;

	CommnunicationNet * communctNet;
	CommnunicationUsb * communctUsb;
	Commnunication * commnunication;


    std::mutex callbackLock;

	std::mutex cmdLock;
    std::mutex waitLock;
	std::condition_variable cmdresp_cond;

    XByteArray cmdresponse;
    std::atomic<uint8_t> currentCmdId;
    std::atomic<bool> bwaitCmdreponse;
    bool bwaitCmdreponseok;

    std::queue< std::shared_ptr<Frame> > rawframeQueue;

    bool UsingImageThread;
    std::thread * threadImageQueue;
    static void ImageQueueFunc(XtDaemon * xtdaemon);
    std::queue< std::shared_ptr<Frame> > imageQueue;


    bool UsingEventThread;
    std::thread * threadEventQueue;
    static void EventQueueFunc(XtDaemon * xtdaemon);
    std::queue< std::shared_ptr<CBEventData> > eventQueue;

    SdkState lastSdkState;
    DevStateCode lastDevState;

    bool WaitFirstConnected;
    uint8_t image_version;

    bool callbackhasParam;
    void * eventIn;
    void * imgIn;
    void * xtsdk;
    uint8_t endianType;

    // 对于imageQueue
    std::mutex imageQueueMutex;
    std::condition_variable imageQueueCV;
    std::mutex eventQueueMutex;
    std::condition_variable eventQueueCV;

    // 对于rawframeQueue
    std::mutex rawframeQueueMutex;
    std::condition_variable rawframeQueueCV;
};

} //end namespace XinTan

