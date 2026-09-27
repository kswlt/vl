/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/

#include "xtdaemon.h"

#include "xtlogger.h"

// 命令返回状态码对应字符串
const std::string CmdRespStr[] = {"ok", "unsupport", "busy", "reject",
                                  "Unknow", "Unknow", "Unknow", "report",
                                  "format", "data", "Unknow", "Unknow",
                                  "Unknow", "Unknow", "Unknow", "timeout"};

namespace XinTan
{

    XtDaemon::XtDaemon(std::string &logtag) : logtagname(logtag)
    {
        event_callback = nullptr;
        img_callback = nullptr;
        internalEvent_callback = nullptr;

        bDaemonRuning = true;

        threadDaemon = nullptr;
        threadReceiver = nullptr;
        threadUdpReceiver = nullptr;
        threadrawFrameProcess = nullptr;

        threadDaemonRuning = true;
        threadReceiveRuning = true;
        threadUdpReceiveRuning = true;
        bDaemonStarted = true;

        rxedPackageN = 0;
        checkLiveCount = 0;
        isCalibrating = false;

        udpPort = 7687;

        communctNet = new CommnunicationNet(ioService, logtag);
        communctUsb = new CommnunicationUsb(ioService, logtag);

        cmd0_checkcount = 0;

        cartesianTransform = new CartesianTransform(logtag);
        baseFilter = new BaseFilter(logtag);

        cmdresponse = {};
        bwaitCmdreponse = false;
        bwaitCmdreponseok = false;

        commnunication = communctNet;
        isSelNet = false;

        UsingImageThread = true;
        UsingEventThread = true;

        threadImageQueue = nullptr;
        threadEventQueue = nullptr;

        needPointcloud = false;

        stateCode = 0;
        sdkState = STATE_UNSTARTUP;
        devState = DevSTATE_DISCONNECTED;

        WaitFirstConnected = true;
        continueNoRespCount = 0;

        fpscounter = 0;
        fps = 0;
        imufpscounter = 0;
        imufps = 0;
        image_version = 100;

        callbackhasParam = false;

        eventIn = nullptr;
        imgIn = nullptr;
        xtsdk = nullptr;

        is_playing = false;

        checkendian_enable = false;

        if (checkendian_enable)
        {
            bneedcheckendian = true;
            set_endianType(Endian_Big); // Endian_Little
        }
        else
        {
            bneedcheckendian = false;
            set_endianType(Endian_Little); // Endian_Little
        }
    }

    XtDaemon::~XtDaemon()
    {
        bDaemonRuning = false;

        {
            std::lock_guard<std::mutex> lock_img(imageQueueMutex);
            imageQueueCV.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock_raw(rawframeQueueMutex);
            rawframeQueueCV.notify_all();
        }

        {
            std::lock_guard<std::mutex> lock_event(eventQueueMutex);
            eventQueueCV.notify_all();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 4. 清理队列资源（避免内存泄漏）
        {
            std::lock_guard<std::mutex> lock_img(imageQueueMutex);
            while (!imageQueue.empty())
            {
                imageQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock_raw(rawframeQueueMutex);
            while (!rawframeQueue.empty())
            {
                rawframeQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock_event(eventQueueMutex);
            while (!eventQueue.empty())
            {
                eventQueue.pop();
            }
        }

        communctNet->disconnect();
        communctNet->closeUdp();
        communctUsb->disconnect();

        std::this_thread::sleep_for(std::chrono::seconds(1));
        delete communctNet;
        delete communctUsb;
        delete cartesianTransform;
        delete baseFilter;

        XTLOGINFO("daemon delete");
    }


    void XtDaemon::set_multicast(bool enable)
    {
        communctNet->UdpMultiCastOn = enable;
    }

    void XtDaemon::set_endianType(uint8_t endian)
    {
        endianType = endian;
        communctNet->endianType = endian;
        communctUsb->endianType = endian;
    }

    void XtDaemon::set_endianCheck(bool enable)
    {
        checkendian_enable = enable;
        if (checkendian_enable)
        {
            bneedcheckendian = true;
            set_endianType(Endian_Big); // Endian_Little
        }
        else
        {
            bneedcheckendian = false;
            set_endianType(Endian_Little); // Endian_Little
        }
    }

    uint8_t XtDaemon::get_endianType()
    {
        return endianType;
    }

    void XtDaemon::setEvtInternalProcCallbk(
        std::function<void(const std::shared_ptr<CBEventData> &, void *)>
            eventcallback,
        void *psdk)
    {
        internalEvent_callback = eventcallback;
        xtsdk = psdk;
    }

    void XtDaemon::setCallback(
        std::function<void(const std::shared_ptr<CBEventData> &)> eventcallback,
        std::function<void(const std::shared_ptr<Frame> &)> imgcallback)
    {

        const std::lock_guard<std::mutex> lock(callbackLock);
        event_callback = eventcallback;
        img_callback = imgcallback;
        eventIn = nullptr;
        imgIn = nullptr;

        callbackhasParam = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    void XtDaemon::setCallback(
        std::function<void(const std::shared_ptr<CBEventData> &, void *)>
            eventcallback,
        std::function<void(const std::shared_ptr<Frame> &, void *)> imgcallback,
        void *peventIn, void *pimgIn)
    {

        const std::lock_guard<std::mutex> lock(callbackLock);
        eventIn_callback = eventcallback;
        imgIn_callback = imgcallback;
        eventIn = peventIn;
        imgIn = pimgIn;

        callbackhasParam = true;

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    void XtDaemon::EventCallback(const std::shared_ptr<CBEventData> &eventdata)
    {
        if (callbackhasParam == false)
        {
            if (event_callback != nullptr)
                event_callback(eventdata);
        }
        else
        {
            if (eventIn_callback != nullptr)
                eventIn_callback(eventdata, eventIn);
        }
    }

    void XtDaemon::ImageCallback(const std::shared_ptr<Frame> &frame)
    {
        if (callbackhasParam == false)
        {
            if (img_callback != nullptr)
                img_callback(frame);
        }
        else
        {
            if (imgIn_callback != nullptr)
                imgIn_callback(frame, imgIn);
        }
    }

    bool XtDaemon::setConnectAddress(std::string connectAddress)
    {
        XTLOGINFO(connectAddress);
        if (connectAddress == commnunication->address)
            return true;

        bool lastIsSelNet = isSelNet;

        if (Utils::ipIsValid(connectAddress))
        {
            commnunication = communctNet;
            isSelNet = true;
        }
        else if (Utils::isComport(connectAddress))
        {
            commnunication = communctUsb;
            isSelNet = false;
        }
        else
            return false;

        commnunication->address = connectAddress;

        if (lastIsSelNet != isSelNet)
        {
            if (lastIsSelNet)
            {
                communctNet->disconnect();
                communctNet->closeUdp();
            }
            else
                communctUsb->disconnect();
        }

        if (sdkState != STATE_UNSTARTUP)
        {
            commnunication->disconnect();
            commnunication->closeUdp();
        }

        return true;
    }

    void XtDaemon::startup()
    {
        XTLOGINFO("");
        
        bDaemonRuning = true;
        if (threadDaemon == nullptr)
        {
            threadDaemon = new std::thread(XtDaemon::DaemonFunc, this);
        }

        if (UsingImageThread)
        {
            if (threadImageQueue == nullptr)
                threadImageQueue = new std::thread(XtDaemon::ImageQueueFunc, this);
        }

        if (UsingEventThread)
        {
            if (threadEventQueue == nullptr)
                threadEventQueue = new std::thread(XtDaemon::EventQueueFunc, this);
        }

        bDaemonStarted = true;
    }

    void XtDaemon::shutdown()
    {
        XTLOGINFO("");
        bDaemonStarted = false;
        bDaemonRuning = false;
        
        commnunication->closeUdp();
        commnunication->disconnect();

        {
            std::lock_guard<std::mutex> lock_img(imageQueueMutex);
            imageQueueCV.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock_raw(rawframeQueueMutex);
            rawframeQueueCV.notify_all();
        }

        {
            std::lock_guard<std::mutex> lock_event(eventQueueMutex);
            eventQueueCV.notify_all();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 4. 清理队列资源（避免内存泄漏）
        {
            std::lock_guard<std::mutex> lock_img(imageQueueMutex);
            while (!imageQueue.empty())
            {
                imageQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock_raw(rawframeQueueMutex);
            while (!rawframeQueue.empty())
            {
                rawframeQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock_event(eventQueueMutex);
            while (!eventQueue.empty())
            {
                eventQueue.pop();
            }
        }

        commnunication->disconnect();
        commnunication->closeUdp();

        updateSdkState(STATE_UNSTARTUP);
        XTLOGINFO("daemon Shutdown completed successfully");
    }

    RespResult XtDaemon::transceiveCmd(uint8_t cmdId, XByteArray data,
                                       uint32_t timeoutms)
    {
        RespResult respResult;

        respResult.ret_code = CmdResp_TIMEOUT;
        if (commnunication->isConnected == false)
            return respResult;

        const std::lock_guard<std::mutex> lock(cmdLock);
        std::unique_lock<std::mutex> lck(waitLock);

        currentCmdId = cmdId;
        cmdresponse = {};
        bool bwaitok = false;
        uint32_t timeout_ms = timeoutms;

        timeout_ms = 300;
        if (timeoutms > 300)
            timeout_ms = timeoutms;

        size_t payload_size = 1 + data.size() + 2;
        XByteArray packagedata;
        packagedata.assign({0x7E, 0xFF, 0xAA, 0x55});

        uint8_t data32[4] = {0};
        Utils::setValueUint32Endian(data32, payload_size, endianType);
        packagedata.push_back(data32[0]);
        packagedata.push_back(data32[1]);
        packagedata.push_back(data32[2]);
        packagedata.push_back(data32[3]);
        packagedata.push_back(static_cast<uint8_t>(cmdId));
        packagedata.insert(packagedata.end(), data.begin(), data.end());
        packagedata.insert(packagedata.end(),
                           {0x00, 0x01, 0xFF, 0x7E, 0x55, 0xAA});

        for (int trys = 0; trys < 4; trys++)
        {
            bwaitCmdreponseok = false;
            bwaitCmdreponse = true;
            commnunication->transmitPackage(packagedata);
            bwaitok = cmdresp_cond.wait_for(lck, std::chrono::milliseconds(timeout_ms), [this]()
                                            { return bwaitCmdreponseok; });
            if (bwaitok == false)
            {
                if ((cmdId == 152) || (cmdId == 153))
                    break;
                if (bwaitCmdreponseok)
                    break;
                XTLOGINFO("try again cmd " + std::to_string(cmdId));
                if (cmdId == 0)
                {
                    if (bneedcheckendian)
                    {
                        cmd0_checkcount++;
                        if (cmd0_checkcount > 2)
                        {
                            cmd0_checkcount = 0;

                            if (endianType == Endian_Little)
                                set_endianType(Endian_Big);
                            else
                                set_endianType(Endian_Little);

                            std::cout << "change edian = " << std::to_string(endianType) << std::endl;

                            if (communctNet->isConnected)
                            {
                                communctNet->disconnect();
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                            }

                            Utils::setValueUint32Endian(&packagedata[4], payload_size, endianType);
                            continue;
                        }
                    }
                    else
                        cmd0_checkcount = 0;
                }
                if (commnunication->isConnected)
                    continue;
            }
            else
                break;
        }

        if (bneedcheckendian)
        {
            if (bwaitok || bwaitCmdreponseok)
                bneedcheckendian = false;
        }
        // bwaitCmdreponseok = false;
        // bwaitCmdreponse = true;
        // commnunication->transmitPackage(packagedata);
        // bwaitok = cmdresp_cond.wait_for(lck, std::chrono::milliseconds(timeout_ms), [this]() { return bwaitCmdreponseok; });
        bwaitCmdreponse = false;

        if (bwaitok)
        {
            respResult.ret_code = (CmdRespCode)(stateCode & 0x0f);
            respResult.data = cmdresponse;

            if (cmdId == 153) // is calibration cmd
                isCalibrating = true;
            else
            {
                if (isCalibrating)
                {
                    if (cmdId != 209)
                        isCalibrating = false;
                }
            }
            continueNoRespCount = 0;
        }
        else
        {
            if (bwaitCmdreponseok) // 没有生效时，用于判断是否命令有响应
            {
                bwaitCmdreponseok = false;
                respResult.ret_code = (CmdRespCode)(stateCode & 0x0f);
                respResult.data = cmdresponse;

                continueNoRespCount = 0;
            }
            else
            {
                respResult.ret_code = CmdResp_TIMEOUT;
                continueNoRespCount++;
                XTLOGWRN("cmd timeout:" + std::to_string(cmdId));
                if (continueNoRespCount > 3)
                {
                    continueNoRespCount = 0;
                    commnunication->disconnect();
                }
            }
        }

        currentCmdId = 0xFF;

        if (respResult.ret_code != CmdResp_OK)
            std::cout << "----Error: Cmd=" + std::to_string(cmdId) +
                             " error= " + CmdRespStr[respResult.ret_code]
                      << std::endl;

        return respResult;
    }

    bool XtDaemon::transmitCmd(uint8_t cmdId, XByteArray data) // 只发不等待回应
    {
        if (commnunication->isConnected == false)
            return {};
        const std::lock_guard<std::mutex> lock(cmdLock);

        size_t payload_size = 1 + data.size() + 2;

        XByteArray packagedata;
        packagedata.assign({0x7E, 0xFF, 0xAA, 0x55});
        uint8_t data32[4] = {0};
        Utils::setValueUint32Endian(data32, payload_size, endianType);
        packagedata.push_back(data32[0]);
        packagedata.push_back(data32[1]);
        packagedata.push_back(data32[2]);
        packagedata.push_back(data32[3]);
        packagedata.push_back(static_cast<uint8_t>(cmdId));
        packagedata.insert(packagedata.end(), data.begin(), data.end());
        packagedata.insert(packagedata.end(),
                           {0x00, 0x01, 0xFF, 0x7E, 0x55, 0xAA});

        return commnunication->transmitPackage(packagedata);
    }

    static int checkTxRxCount = 0;
    void XtDaemon::DaemonFunc(XtDaemon *xtdaemon)
    {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTDaemonTh");
#endif
        std::this_thread::sleep_for(std::chrono::seconds(3));
        XTLOGINFOEXT(xtdaemon->logtagname, "");
        while (xtdaemon->bDaemonRuning)
        {

            if (xtdaemon->threadrawFrameProcess == nullptr)
            {
                xtdaemon->threadrawFrameProcess =
                    new std::thread(XtDaemon::rawFrameQThreadFunc, xtdaemon);
            }

            if (xtdaemon->threadReceiver == nullptr)
            {
                xtdaemon->threadReceiver =
                    new std::thread(XtDaemon::ReceiveThreadFunc, xtdaemon);
            }

            if ((xtdaemon->isSelNet) &&
                (xtdaemon->threadUdpReceiver == nullptr))
            {
                xtdaemon->threadUdpReceiver =
                    new std::thread(XtDaemon::UdpReceiveThreadFunc, xtdaemon);
            }

            // 判断通路是否通
            if (xtdaemon->rxedPackageN == 0)
                xtdaemon->checkLiveCount += 1;
            else
            {
                xtdaemon->checkLiveCount = 0;
                xtdaemon->rxedPackageN = 0;
            }

            if (xtdaemon->commnunication->isConnected)
            {
                if (xtdaemon->isCalibrating)
                {
                    if (xtdaemon->checkLiveCount > 29)
                    {

                        if (xtdaemon->checkLiveCount > 31)
                        {
                            xtdaemon->checkLiveCount = 0;
                            xtdaemon->updateSdkState(STATE_TXRX_VERIFYING);
                        }
                        else
                            xtdaemon->transceiveCmd(0, {}); // 再发一次检查连接
                    }
                }
                else
                {
                    if (xtdaemon->checkLiveCount > 2)
                    {

                        if (xtdaemon->checkLiveCount > 3)
                        {
                            xtdaemon->checkLiveCount = 0;
                            xtdaemon->updateSdkState(STATE_TXRX_VERIFYING);
                        }
                        else
                            xtdaemon->transceiveCmd(0, {}); // 再发一次检查连接
                    }
                }
                if (xtdaemon->sdkState == STATE_TXRX_VERIFYING)
                {
                    if (checkTxRxCount > 2)
                    {
                        checkTxRxCount = 0;

                        XTLOGINFOEXT(xtdaemon->logtagname, "Try Check TXRX");
                        xtdaemon->transceiveCmd(0, {}); // 再发一次检查连接
                    }
                    else
                        checkTxRxCount++;
                }
            }

            if (xtdaemon->isSelNet)
            {
                if (xtdaemon->communctUsb->isConnected)
                {
                    xtdaemon->communctUsb->disconnect();
                }
            }
            else
            {

                if (xtdaemon->communctNet->isConnected)
                {
                    xtdaemon->communctNet->disconnect();
                }
                if (xtdaemon->communctNet->isUdpOpened)
                {
                    xtdaemon->communctNet->closeUdp();
                }
            }

            xtdaemon->fps = xtdaemon->fpscounter;
            xtdaemon->fpscounter = 0;
            xtdaemon->imufps = xtdaemon->imufpscounter;
            xtdaemon->imufpscounter = 0;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void XtDaemon::ReceiveThreadFunc(XtDaemon *xtdaemon)
    {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTReceiveTh");
#endif
        XByteArray pkgData;
        while (xtdaemon->bDaemonRuning)
        {
            if (xtdaemon->bDaemonStarted)
            {
                // #检查端口是否打开
                if (xtdaemon->commnunication->isConnected == false)
                {
                    xtdaemon->updateSdkState(STATE_PORTOPENING);

                    if (xtdaemon->commnunication->connect())
                    {
                        xtdaemon->currentCmdId = 0;
                        xtdaemon->bwaitCmdreponse = false;
                        xtdaemon->bwaitCmdreponseok = false;
                        // xtdaemon->set_endianType(Endian_Little);
                        xtdaemon->cmd0_checkcount = 0;
                        if (xtdaemon->checkendian_enable)
                            xtdaemon->bneedcheckendian = true;

                        xtdaemon->updateSdkState(STATE_TXRX_VERIFYING);
                        xtdaemon->transmitCmd(0, {}); // 立即发一次检查连接
                    }
                    else
                        std::this_thread::sleep_for(std::chrono::seconds(2));

                    continue;
                }
                // #收数据包
                if (xtdaemon->commnunication->receivePackage(
                        pkgData)) // 头去8，尾去4之后的
                {
                    uint32_t pkgDatasize = pkgData.size();
                    uint8_t cmdid = pkgData[0];
                    uint8_t version = pkgData[pkgDatasize - 1];
                    uint8_t devstate = 0;
                    uint8_t respcode = 0;

                    xtdaemon->bneedcheckendian = false;

                    if ((version == 2) || (version == 1)) // 1是为了BootLoader
                        devstate = (pkgData[pkgDatasize - 2] >> 4) & 0x0f;
                    else if (version == 3)
                        devstate = pkgData[pkgDatasize - 2];

                    if (cmdid != 251) // 不是frame数据
                    {
                        bool waitcmd_ok = false;
                        XByteArray respdata;
                        if ((version == 2) || (version == 1)) // 1是为了BootLoader
                        {
                            respcode = pkgData[pkgDatasize - 2] & 0x0f;

                            respdata.assign(pkgData.begin() + 1,
                                            pkgData.end() - 2);
                        }
                        else if (version == 3)
                        {
                            respcode = pkgData[pkgDatasize - 3];

                            respdata.assign(pkgData.begin() + 1,
                                            pkgData.end() - 3);
                        }

                        if (xtdaemon->bwaitCmdreponse) // 响应命令数据
                        {
                            if (xtdaemon->currentCmdId == cmdid)
                            {
                                xtdaemon->stateCode = static_cast<CmdRespCode>(respcode);
                                xtdaemon->cmdresponse = respdata;
                                xtdaemon->bwaitCmdreponseok = true; // 没有生效时，用于判断是否命令有响应
                                xtdaemon->bwaitCmdreponse = false;
                                waitcmd_ok = true;

                                xtdaemon->cmdresp_cond.notify_one(); // notify_all
                            }
                        }

                        if (waitcmd_ok == false) // 上报数据事件
                        {
                            std::shared_ptr<CBEventData> eventdata =
                                std::shared_ptr<CBEventData>(new CBEventData(
                                    "data", cmdid, respdata, respcode));

                            if ((xtdaemon->UsingEventThread) && (cmdid != 157))
                            {
                                xtdaemon->eventQueue.push(eventdata);
                                xtdaemon->eventQueueCV.notify_one();
                            }
                            else
                            {
                                const std::lock_guard<std::mutex> lock(
                                    xtdaemon->callbackLock);
                                xtdaemon->EventCallback(eventdata);
                            }

                            // std::cout << "cmdresp Event = " +std::to_string(cmdid) << std::endl;

                            if (cmdid == 153) // is calibration cmd
                                xtdaemon->isCalibrating = true;
                            if (cmdid == 252)
                                xtdaemon->imufpscounter++;
                        }

                        if (xtdaemon->sdkState != STATE_CONNECTED)
                        {
                            xtdaemon->updateSdkState(STATE_CONNECTED);
                        }
                    }
                    else // frame包
                    {
                        if (xtdaemon->ParseFrame(pkgData) == false)
                            std::cout << "frame error " << std::endl;
                    }

                    if (devstate > DevSTATE_ERR_MAX)
                        devstate = DevSTATE_ERR_MAX;

                    if (devstate <= DevSTATE_ERR_MAX)
                    {
                        DevStateCode dev_state = (DevStateCode)devstate;

                        if (dev_state !=
                            xtdaemon->devState) // device state changed
                        {
                            xtdaemon->updateDevState(dev_state);
                        }
                    }
                }
                else
                {
                    if (xtdaemon->commnunication->isConnected == false)
                    {
                        xtdaemon->updateSdkState(STATE_TXRX_VERIFYING);
                    }
                    continue;
                }

                // 收到数据包
                xtdaemon->rxedPackageN += 1;
                // std::this_thread::sleep_for(std::chrono::milliseconds(2)); // usb读取间隙一下
            }
            else
            {
                if (xtdaemon->commnunication->isConnected)
                    xtdaemon->commnunication->disconnect();

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    bool XtDaemon::ParseFrame(XByteArray &frameData, std::string frame_label)
    {
        uint32_t pkgDatasize = frameData.size();
        uint8_t cmdid = frameData[0];
        uint8_t version = frameData[pkgDatasize - 1];
        uint8_t endian = Endian_Big;

        if (version == 3)
            endian = Endian_Little;

        if (endianType != endian)
            set_endianType(endian);

        int dataType = (int)frameData[1];
        uint16_t Frame_id = Utils::getValueUint16Endian(&(frameData[2]), endian);
        int width = Utils::getValueUint16Endian(&(frameData[4]), endian);
        int height = Utils::getValueUint16Endian(&(frameData[6]), endian);

        if ((width > 320) || (width < 1))
            return false;
        if ((height > 240) || (height < 1))
            return false;

        std::shared_ptr<Frame> frame = std::make_shared<Frame>(
            logtagname, dataType, Frame_id, width, height, 16);
        if (version == 2)
        {
            uint32_t frame_temptpos = pkgDatasize - 52;

            frame->frame_version = frameData[pkgDatasize - 1];
            frame->temperature =
                frameData[frame_temptpos + 1] | frameData[frame_temptpos] << 8;

            frame->roi_x0 = (uint16_t)frameData[frame_temptpos + 3] |
                            (uint16_t)frameData[frame_temptpos + 2] << 8;
            frame->roi_y0 = (uint16_t)frameData[frame_temptpos + 5] |
                            (uint16_t)frameData[frame_temptpos + 4] << 8;

            if (frame->roi_x0 > 310)
                frame->roi_x0 = 0;

            if (frame->roi_y0 > 110)
                frame->roi_y0 = 0;

            uint8_t binning = frameData[frame_temptpos + 6];
            frame->binning = 0;
            if (binning & 0x0F) // v binning
                frame->binning |= 0x01;
            if (binning & 0xF0) // h binning
                frame->binning |= 0x02;

            frame->vcseltemperature = frameData[frame_temptpos + 10] |
                                      frameData[frame_temptpos + 9] << 8;

            frame->timeStampS = Utils::getValueUint48Endian(
                &(frameData[pkgDatasize - 14]), Endian_Big);
            frame->timeStampNS = Utils::getValueUint32Endian(
                &(frameData[pkgDatasize - 8]), Endian_Big);
            frame->timeStampState = frameData[pkgDatasize - 4];
            frame->timeStampType = frameData[pkgDatasize - 3];
        }
        else if (version == 3)
        {
            uint8_t *pimu = (uint8_t *)(&frame->imu);
            uint8_t *pinfo = (uint8_t *)(&frame->info);
            uint16_t datainfosize = Utils::getValueUint16Endian(
                &(frameData[pkgDatasize - 4]), Endian_Little);

            if (datainfosize != sizeof(frame->info))
            {
                std::cout << "size error: " + std::to_string(datainfosize)
                          << "  " + std::to_string(sizeof(frame->info))
                          << std::endl;
                return false;
            }

            int infopos = pkgDatasize - datainfosize;
            for (int i = 0; i < datainfosize; i++)
                pinfo[i] = frameData[infopos + i];

            frame->frame_version = frame->info.ptclversion;
            frame->temperature = frame->info.temperature[0];
            frame->vcseltemperature = frame->info.temperature[1];
            frame->roi_x0 = frame->info.roix[0];
            frame->roi_y0 = frame->info.roiy[0];

            frame->binning = frame->info.binning;

            frame->timeStampS = frame->info.timestamp[0] / 1000;
            frame->timeStampNS = (frame->info.timestamp[0] % 1000) * 1000000;
            frame->timeStampState = frame->info.timesync_state;
            frame->timeStampType = frame->info.timesync_type;

            if (frame->info.imusize == sizeof(frame->imu))
            {
                uint16_t imusize = frame->info.imusize;
                int imupos = infopos - imusize;
                for (int i = 0; i < imusize; i++)
                    pimu[i] = frameData[imupos + i];

                if (frame->imu.mark != 0x55AA)
                    frame->imu.flags = 0;
            }
            else
                frame->imu.flags = 0;

            CamParameterS camparamter;
            memcpy(&camparamter, frame->info.lensparameters, sizeof(camparamter));
            cartesianTransform->maptable(camparamter);
            if (frame_label == "")
            {
                std::string sn_str(reinterpret_cast<char *>(frame->info.sn), 29);
                std::string sn_str_without_nulls;
                sn_str_without_nulls.reserve(29); // Reserve space for efficiency

                for (char c : sn_str)
                {
                    if (c != '\x00')
                    {
                        sn_str_without_nulls.push_back(c);
                    }
                }

                frame->frame_label = sn_str_without_nulls + "_" + std::to_string(frame->frame_id);
            }
            else
            {
                frame->frame_label = frame_label;
            }

            // frame_label.erase(std::remove(frame_label.begin(), frame_label.end(), '\x00'), frame_label.end());
        }

        XByteArray startmark = {0x7E, 0xFF, 0xAA, 0x55};
        XByteArray endmark = {0xFF, 0x7E, 0x55, 0xAA};
        XByteArray payloadlen = {0, 0, 0, 0};
        Utils::setValueUint32Endian(&payloadlen[0], pkgDatasize, endianType);

        frame->frameData.assign(frameData.begin(), frameData.end());
        // 添加头和尾
        frame->frameData.insert(frame->frameData.begin(), payloadlen.begin(), payloadlen.end());
        frame->frameData.insert(frame->frameData.begin(), startmark.begin(), startmark.end());
        frame->frameData.insert(frame->frameData.end(), endmark.begin(), endmark.end());
        {
            std::lock_guard<std::mutex> lock(rawframeQueueMutex);
            rawframeQueue.push(frame);
        }
        rawframeQueueCV.notify_one(); // 通知处理线程
        fpscounter++;

        return true;
    }

    void XtDaemon::UdpReceiveThreadFunc(XtDaemon *xtdaemon)
    {

#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTUdpRecTh");
#endif
        XByteArray pkgData;

        XTLOGINFOEXT(xtdaemon->logtagname, "isSelNet=" + std::to_string(xtdaemon->isSelNet));
        
        while (xtdaemon->bDaemonRuning)
        {
            if (xtdaemon->isSelNet)
            {
                // #检查socket是否打开
                if (xtdaemon->communctNet->isUdpOpened == false)
                {
                    XTLOGINFOEXT(xtdaemon->logtagname, "isUdpOpened=" + std::to_string(xtdaemon->communctNet->isUdpOpened)); 
                    if (xtdaemon->communctNet->openUdp(xtdaemon->udpPort))
                    {
                    }
                    else
                        std::this_thread::sleep_for(std::chrono::seconds(2));

                    continue;
                }

                // #收数据包
                if (xtdaemon->communctNet->udpReceiveFrame(pkgData))
                {
                    uint32_t datasize = pkgData.size();
                    if (pkgData[0] == 252) // imu frame
                    {
                        XByteArray imudata;
                        imudata.assign(pkgData.begin() + 1, pkgData.end() - 3);
                        std::shared_ptr<CBEventData> eventdata =
                            std::shared_ptr<CBEventData>(new CBEventData(
                                "data", pkgData[0], imudata, pkgData[datasize - 3]));

                        xtdaemon->eventQueue.push(eventdata);
                        xtdaemon->eventQueueCV.notify_one();

                        xtdaemon->imufpscounter++;

                        continue;
                    }

                    if (datasize < 1000)
                        continue;

                    if (xtdaemon->ParseFrame(pkgData) == false)
                        std::cout << "frame error " << std::endl;

                    // xtdaemon->rxedPackageN += 1;
                    uint8_t version = pkgData[datasize - 1];
                    uint8_t devstatevalue = (pkgData[datasize - 2] >> 4) & 0x0f;
                    if (version == 3)
                        devstatevalue = pkgData[datasize - 2];

                    if (devstatevalue > DevSTATE_ERR_MAX)
                        devstatevalue = DevSTATE_ERR_MAX;

                    if (xtdaemon->sdkState != STATE_CONNECTED)
                    {
                        xtdaemon->updateSdkState(STATE_UDPIMGOK);
                    }
                    else
                    {
                        if (devstatevalue <= DevSTATE_ERR_MAX)
                        {
                            DevStateCode dev_state = (DevStateCode)devstatevalue;
                            if ((dev_state > 0) &&
                                (dev_state !=
                                 xtdaemon->devState)) // device state changed
                            {
                                xtdaemon->updateDevState(dev_state);
                            }
                        }
                    }
                }
            }
            else
            {

                if (xtdaemon->communctNet->isUdpOpened)
                    xtdaemon->communctNet->closeUdp();

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    void XtDaemon::EventQueueFunc(XtDaemon *xtdaemon)
    {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTEventTh");
#endif
        while (xtdaemon->bDaemonRuning)
        {
            std::unique_lock<std::mutex> lock(xtdaemon->eventQueueMutex);

            xtdaemon->eventQueueCV.wait(lock, [xtdaemon]
                                        { return !xtdaemon->bDaemonRuning || !xtdaemon->eventQueue.empty(); });
            if (!xtdaemon->bDaemonRuning)
                break;

            while (xtdaemon->eventQueue.size() > 0)
            // if ((xtdaemon->eventQueue.empty() == false))
            {
                const std::shared_ptr<CBEventData> &eventData =
                    xtdaemon->eventQueue.front();

                if ((xtdaemon->sdkState == STATE_CONNECTED) &&
                    (eventData->cmdid == 0xfe)) // 端口打开后第一次连接上设备
                    xtdaemon->internalEvent_callback(eventData, xtdaemon->xtsdk);

                {
                    const std::lock_guard<std::mutex> lock(
                        xtdaemon->callbackLock);
                    xtdaemon->EventCallback(eventData);
                }
                xtdaemon->eventQueue.pop();
            }
            // std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // void XtDaemon::ImageQueueFunc(XtDaemon *xtdaemon)
    // {
    //     while (xtdaemon->bDaemonRuning)
    //     {
    //         if (xtdaemon->imageQueue.empty() == false)
    //         {
    //             while (xtdaemon->imageQueue.size() > 1)
    //             {
    //                 xtdaemon->imageQueue.pop();
    //                 XTLOGWRNEXT(xtdaemon->logtagname, "image discard");
    //                 std::cout << "image  discard " << std::endl;
    //             }
    //             if (xtdaemon->imageQueue.empty() == false)
    //             {
    //                 const std::shared_ptr<Frame> &frame = xtdaemon->imageQueue.front();

    //                 {
    //                     const std::lock_guard<std::mutex> lock(xtdaemon->callbackLock);
    //                     if (xtdaemon->sdkState != STATE_TXRX_VERIFYING) // && (xtdaemon->sdkState != STATE_UDPIMGOK))
    //                         xtdaemon->ImageCallback(frame);
    //                 }
    //                 xtdaemon->imageQueue.pop();
    //             }
    //         }
    //         std::this_thread::sleep_for(std::chrono::milliseconds(30));
    //     }
    // }

    // void XtDaemon::rawFrameQThreadFunc(XtDaemon *xtdaemon)
    // {
    //     while (1)
    //     {
    //         if (xtdaemon->rawframeQueue.empty() == false)
    //         {
    //             while (xtdaemon->rawframeQueue.size() > 1)
    //             {
    //                 xtdaemon->rawframeQueue.pop();
    //                 XTLOGWRNEXT(xtdaemon->logtagname, "frame discard");
    //                 std::cout << "frame  discard " << std::endl;
    //             }
    //             if (xtdaemon->rawframeQueue.empty() == false)
    //             {
    //                 // TicToc tic;
    //                 const std::shared_ptr<Frame> &frame = xtdaemon->rawframeQueue.front();
    //                 frame->resetData();
    //                 frame->sortData(frame->frameData);
    //                 xtdaemon->reportImage(frame);
    //                 xtdaemon->rawframeQueue.pop();
    //                 // std::cout << "whole time: " << tic.toc() << std::endl;
    //             }
    //         }
    //         std::this_thread::sleep_for(std::chrono::milliseconds(30));
    //     }

    //     xtdaemon->threadrawFrameProcess = nullptr;
    // }

    void XtDaemon::ImageQueueFunc(XtDaemon *xtdaemon)
    {

#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTImageTh");
#endif
        while (xtdaemon->bDaemonRuning)
        {
            std::shared_ptr<Frame> frame;
            {
                std::unique_lock<std::mutex> lock(xtdaemon->imageQueueMutex);
                xtdaemon->imageQueueCV.wait(lock, [xtdaemon]
                                            { return xtdaemon->bDaemonRuning == false || !xtdaemon->imageQueue.empty(); });
                if (!xtdaemon->bDaemonRuning)
                    break;
                // 确保只保留最新一帧
                while (xtdaemon->imageQueue.size() > 1)
                {
                    xtdaemon->imageQueue.pop();
                    XTLOGWRNEXT(xtdaemon->logtagname, "image discard");
                }
                frame = xtdaemon->imageQueue.front();
                xtdaemon->imageQueue.pop(); // 立即弹出，防止其他线程干扰
            } // 自动释放锁
            // 执行回调
            {
                std::lock_guard<std::mutex> cbLock(xtdaemon->callbackLock);
                // std::cout << "xtdaemon->is_playing: " << xtdaemon->is_playing << std::endl;
                if (xtdaemon->sdkState != STATE_TXRX_VERIFYING || xtdaemon->is_playing)
                {
                    xtdaemon->ImageCallback(frame);
                }
            }
        }
    }

    void XtDaemon::rawFrameQThreadFunc(XtDaemon *xtdaemon)
    {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "XTrawFramTh");
#endif
        while (xtdaemon->bDaemonRuning)
        {
            std::unique_lock<std::mutex> lock(xtdaemon->rawframeQueueMutex);
            xtdaemon->rawframeQueueCV.wait(lock, [xtdaemon]
                                           { return xtdaemon->bDaemonRuning == false || !xtdaemon->rawframeQueue.empty(); });

            if (!xtdaemon->bDaemonRuning)
                break;

            // 丢弃旧帧，保留最新一帧
            while (xtdaemon->rawframeQueue.size() > 1)
            {
                xtdaemon->rawframeQueue.pop();
                XTLOGWRNEXT(xtdaemon->logtagname, "frame discard");
            }

            auto frame = xtdaemon->rawframeQueue.front();
            lock.unlock(); // 释放队列锁

            frame->resetData();
            frame->sortData(frame->frameData);
            xtdaemon->reportImage(frame);

            lock.lock();
            xtdaemon->rawframeQueue.pop();
        }
        xtdaemon->threadrawFrameProcess = nullptr;
    }

    void XtDaemon::updateSdkState(SdkState state)
    {
        if (sdkState != state)
        {
            sdkState = state;
            uint8_t cmdid = 0xff;

            if (sdkState == STATE_PORTOPENING)
                WaitFirstConnected = true;

            if (sdkState == STATE_TXRX_VERIFYING)
            {
                devState = DevSTATE_DISCONNECTED;
                checkTxRxCount = 0;
            }

            if (sdkState == STATE_UDPIMGOK)
                devState = DevSTATE_STREAM;

            if (WaitFirstConnected)
            {
                if (sdkState == STATE_CONNECTED)
                {
                    WaitFirstConnected = false;
                    cmdid = 0xfe;
                }
            }

            reportEvent("sdkState", cmdid, {(uint8_t)state});
        }
    }

    void XtDaemon::reportEvent(std::string eventstr, uint8_t cmdid,
                               XByteArray data, uint8_t cmdstate)
    {
        std::shared_ptr<CBEventData> eventdata = std::shared_ptr<CBEventData>(
            new CBEventData(eventstr, cmdid, data, cmdstate));

        if (UsingEventThread)
        {
            eventQueue.push(eventdata);
            eventQueueCV.notify_one();
        }
        else
        {
            const std::lock_guard<std::mutex> lock(callbackLock);
            EventCallback(eventdata);
        }
    }

    // void XtDaemon::reportImage(const std::shared_ptr<Frame> &frame)
    // {

    //     if (image_version != frame->frame_version)
    //     {
    //         image_version = frame->frame_version;
    //         XTLOGINFO("imageversion=" + std::to_string(frame->frame_version));
    //     }

    //     if (UsingImageThread)
    //     {
    //         baseFilter->doBaseFilter(frame);
    //         if (needPointcloud)
    //         {
    //             cartesianTransform->pcltransCamparm(frame);
    //         }
    //         imageQueue.push(frame);
    //     }
    //     else
    //     {
    //         const std::lock_guard<std::mutex> lock(callbackLock);
    //         ImageCallback(frame);
    //     }
    // }

    void XtDaemon::reportImage(const std::shared_ptr<Frame> &frame)
    {

        if (image_version != frame->frame_version)
        {
            image_version = frame->frame_version;
            XTLOGINFO("imageversion=" + std::to_string(frame->frame_version));
        }

        if (UsingImageThread)
        {
            baseFilter->doBaseFilter(frame);
            if (needPointcloud)
            {
                cartesianTransform->pcltransCamparm(frame);
            }
            {
                std::lock_guard<std::mutex> lock(imageQueueMutex);
                imageQueue.push(frame);
            }
            imageQueueCV.notify_one(); // 通知处理线程
        }
        else
        {
            const std::lock_guard<std::mutex> lock(callbackLock);
            ImageCallback(frame);
        }
    }

    void XtDaemon::updateDevState(DevStateCode state)
    {
        if (devState != state)
        {
            devState = state;
            reportEvent("devState", 0xFF, {(uint8_t)state});

            if (devState < DevSTATE_ERR_MAX)
                XTLOGINFO(devStateStr[devState]);
            else
                XTLOGINFO("unknow:" + std::to_string(devState));
        }
    }

    void XtDaemon::doDataFrame(const std::vector<uint8_t> &frameData, std::string frame_label)
    {
        if (ParseFrame((XByteArray &)frameData, frame_label) == false)
            std::cout << "frame error " << std::endl;
    }

    bool XtDaemon::doUdpFrameData(const std::vector<uint8_t> &udpframeData, std::string frame_label)
    {
        uint8_t endian = Endian_Little;
        uint32_t totalsize = Utils::getValueUint32Endian(&udpframeData[2], endian);
        uint16_t framesn = Utils::getValueUint16Endian(&udpframeData[0], endian);

        uint32_t udpsize = udpframeData.size();
        uint32_t udp_pkg_count = udpsize / 1420;
        uint32_t framedatasize = 0;
        if (udpsize % 1420 > 0)
            udp_pkg_count += 1;

        framedatasize = udpsize - udp_pkg_count * 20;

        // check data
        if (totalsize != framedatasize)
        {
            endian = Endian_Big;
            totalsize = Utils::getValueUint32Endian(&udpframeData[2], endian);
            framesn = Utils::getValueUint16Endian(&udpframeData[0], endian);

            if (totalsize != framedatasize)
                return false;
        }

        XByteArray frameData;
        frameData.resize(totalsize);

        for (int i = 0; i < udp_pkg_count; i++)
        {
            uint16_t framesn2 =
                Utils::getValueUint16Endian(&udpframeData[i * 1420 + 0], endian);
            uint32_t totalsize2 =
                Utils::getValueUint32Endian(&udpframeData[i * 1420 + 2], endian);
            uint16_t payloadSize2 =
                Utils::getValueUint16Endian(&udpframeData[i * 1420 + 6], endian);
            uint32_t sentsize2 =
                Utils::getValueUint32Endian(&udpframeData[i * 1420 + 8], endian);

            if (payloadSize2 > 1400)
                return false;

            if ((framesn2 != framesn) || (totalsize != totalsize2) ||
                (sentsize2 > (totalsize - payloadSize2)))
                return false;

            memcpy(&frameData[sentsize2], &udpframeData[i * 1420 + 20], payloadSize2);
        }

        frameData.erase(frameData.begin(), frameData.begin() + 8);
        frameData.erase(frameData.end() - 4, frameData.end());
        doDataFrame(frameData, frame_label);
        return true;
    }

} // end namespace XinTan
