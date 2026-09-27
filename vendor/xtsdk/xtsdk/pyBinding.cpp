#define PYBIND11_DETAILED_ERROR_MESSAGES
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/stl_bind.h>
#include <pybind11/chrono.h>
#include <pybind11/numpy.h>
#include "frame.h"
#include "xtsdk.h"

namespace py = pybind11;
// PYBIND11_MAKE_OPAQUE(std::vector<uint8_t>);  // 假设 XByteArray 是 std::vector<uint8_t>
PYBIND11_MODULE(xintan_sdk, m)
{
    m.doc() = "Xintan SDK Python bindings";

    // Binding for XByteArray
    py::bind_vector<std::vector<uint8_t>>(m, "XByteArray");

    py::class_<XinTan::XtPointXYZI>(m, "XtPointXYZI")
        .def(py::init<>())
        .def_readwrite("x", &XinTan::XtPointXYZI::x)
        .def_readwrite("y", &XinTan::XtPointXYZI::y)
        .def_readwrite("z", &XinTan::XtPointXYZI::z)
        .def_readwrite("intensity", &XinTan::XtPointXYZI::intensity);

    py::class_<XinTan::CamParameterS>(m, "CamParameterS")
        .def(py::init<>())
        .def_readwrite("fx", &XinTan::CamParameterS::fx)
        .def_readwrite("fy", &XinTan::CamParameterS::fy)
        .def_readwrite("cx", &XinTan::CamParameterS::cx)
        .def_readwrite("cy", &XinTan::CamParameterS::cy)
        .def_readwrite("k1", &XinTan::CamParameterS::k1)
        .def_readwrite("k2", &XinTan::CamParameterS::k2)
        .def_readwrite("k3", &XinTan::CamParameterS::k3)
        .def_readwrite("p1", &XinTan::CamParameterS::p1)
        .def_readwrite("p2", &XinTan::CamParameterS::p2);

    py::class_<XinTan::ExtrinsicIMULidar>(m, "ExtrinsicIMULidar")
        .def(py::init<>())
        .def_readwrite("qw", &XinTan::ExtrinsicIMULidar::qw)
        .def_readwrite("qx", &XinTan::ExtrinsicIMULidar::qx)
        .def_readwrite("qy", &XinTan::ExtrinsicIMULidar::qy)
        .def_readwrite("qz", &XinTan::ExtrinsicIMULidar::qz)
        .def_readwrite("tx", &XinTan::ExtrinsicIMULidar::tx)
        .def_readwrite("ty", &XinTan::ExtrinsicIMULidar::ty)
        .def_readwrite("tz", &XinTan::ExtrinsicIMULidar::tz);

    py::enum_<XinTan::Frame::DataType>(m, "DataType")
        .value("AMPLITUDE", XinTan::Frame::AMPLITUDE)
        .value("DISTANCE", XinTan::Frame::DISTANCE)
        .value("RESERVER", XinTan::Frame::RESERVER)
        .value("GRAYSCALE", XinTan::Frame::GRAYSCALE)
        .export_values();

    py::class_<XinTan::Frame, std::shared_ptr<XinTan::Frame>>(m, "Frame")
        .def(py::init<std::string &, uint16_t, uint64_t, uint16_t, uint16_t, uint16_t>())
        // 成员变量的绑定
        .def_readwrite("frame_version", &XinTan::Frame::frame_version)
        .def_readwrite("pixelDataOffset", &XinTan::Frame::pixelDataOffset)
        .def_readwrite("frame_id", &XinTan::Frame::frame_id)
        .def_readwrite("dataType", &XinTan::Frame::dataType)
        .def_readwrite("width", &XinTan::Frame::width)
        .def_readwrite("height", &XinTan::Frame::height)
        .def_readwrite("px_size", &XinTan::Frame::px_size)
        .def_readwrite("roi_x0", &XinTan::Frame::roi_x0)
        .def_readwrite("roi_y0", &XinTan::Frame::roi_y0)
        .def_readwrite("binning", &XinTan::Frame::binning)
        .def_readwrite("timeStampS", &XinTan::Frame::timeStampS)
        .def_readwrite("timeStampNS", &XinTan::Frame::timeStampNS)
        .def_readwrite("timeStampState", &XinTan::Frame::timeStampState)
        .def_readwrite("timeStampType", &XinTan::Frame::timeStampType)
        .def_readwrite("temperature", &XinTan::Frame::temperature)
        .def_readwrite("vcseltemperature", &XinTan::Frame::vcseltemperature)
        .def_readwrite("dust_percent", &XinTan::Frame::dust_percent)
        .def_readwrite("needxiacaiyang", &XinTan::Frame::needxiacaiyang)
        .def_readwrite("xbinning", &XinTan::Frame::xbinning)
        .def_readwrite("ybinning", &XinTan::Frame::ybinning)
        .def_readwrite("orgwidth", &XinTan::Frame::orgwidth)
        .def_readwrite("orgheight", &XinTan::Frame::orgheight)
        .def_readwrite("hasPointcloud", &XinTan::Frame::hasPointcloud)
        .def_readwrite("frameData", &XinTan::Frame::frameData)
        .def_readwrite("leveldata", &XinTan::Frame::leveldata)
        .def_readwrite("rawdistData", &XinTan::Frame::rawdistData)
        .def_readwrite("distData", &XinTan::Frame::distData)
        .def_readwrite("amplData", &XinTan::Frame::amplData)
        .def_readwrite("freqMap", &XinTan::Frame::freqMap)
        .def_readwrite("intMap", &XinTan::Frame::intMap)
        .def_readwrite("grayscaledata", &XinTan::Frame::grayscaledata)
        .def_readwrite("reflectivity", &XinTan::Frame::reflectivity)
        .def_readwrite("motion_vec", &XinTan::Frame::motion_vec)
        .def_readwrite("points", &XinTan::Frame::points)
        .def_readwrite("frame_label", &XinTan::Frame::frame_label)
        // .def_readwrite("logtagname", &XinTan::Frame::logtagname)


        // 绑定常用的 getter 和 setter 方法
        .def("sortData", &XinTan::Frame::sortData)
        .def("resetData", &XinTan::Frame::resetData)
        .def("setCofArray", &XinTan::Frame::setCofArray)

        .def("getImageFlags", &XinTan::Frame::getImageFlags)
        .def("getDistDataSize", &XinTan::Frame::getDistDataSize)
        .def("getDistData", &XinTan::Frame::getDistData, py::arg("index"))
        .def("getAmplData", &XinTan::Frame::getAmplData, py::arg("index"))
        .def("getMaxAmplData", &XinTan::Frame::getMaxAmplData)
        .def("getRawDistData", &XinTan::Frame::getRawDistData, py::arg("index"))
        .def("getGrayscaleData", &XinTan::Frame::getGrayscaleData, py::arg("index"))
        .def("getReflectivity", &XinTan::Frame::getReflectivity, py::arg("index"))

        .def("getleveldata", &XinTan::Frame::getleveldata, py::arg("index"))
        .def("getMotionFlag", &XinTan::Frame::getMotionFlag, py::arg("index"))
        .def("getFreqMap", &XinTan::Frame::getFreqMap, py::arg("index"))
        .def("getIntMap", &XinTan::Frame::getIntMap, py::arg("index"))


        .def("getDistDataBuffer", &XinTan::Frame::getDistDataBuffer)
        .def("getAmplDataBuffer", &XinTan::Frame::getAmplDataBuffer)
        .def("getRawDistDataBuffer", &XinTan::Frame::getRawDistDataBuffer)
        .def("getGrayscaleDataBuffer", &XinTan::Frame::getGrayscaleDataBuffer)
        .def("getReflectivityBuffer", &XinTan::Frame::getReflectivityBuffer)
        .def("getleveldataBuffer", &XinTan::Frame::getleveldataBuffer)
        .def("getMotionFlagBuffer", &XinTan::Frame::getMotionFlagBuffer)
        .def("getFreqMapBuffer", &XinTan::Frame::getFreqMapBuffer)
        .def("getIntMapBuffer", &XinTan::Frame::getIntMapBuffer)



        // 纯虚函数声明
        .def("getFrameVersion", &XinTan::Frame::getFrameVersion)
        .def("getPixelDataOffset", &XinTan::Frame::getPixelDataOffset)
        .def("getFrameId", &XinTan::Frame::getFrameId)
        .def("getDataType", &XinTan::Frame::getDataType)
        .def("getWidth", &XinTan::Frame::getWidth)
        .def("getHeight", &XinTan::Frame::getHeight)
        .def("getPxSize", &XinTan::Frame::getPxSize)
        .def("getRoiX0", &XinTan::Frame::getRoiX0)
        .def("getRoiY0", &XinTan::Frame::getRoiY0)
        .def("getBinning", &XinTan::Frame::getBinning)
        .def("getTimeStampS", &XinTan::Frame::getTimeStampS)
        .def("getTimeStampNS", &XinTan::Frame::getTimeStampNS)
        .def("getTimeStampState", &XinTan::Frame::getTimeStampState)
        .def("getTimeStampType", &XinTan::Frame::getTimeStampType)
        .def("getTemperature", &XinTan::Frame::getTemperature)
        .def("getVcselTemperature", &XinTan::Frame::getVcselTemperature)
        .def("getDustPercent", &XinTan::Frame::getDustPercent)
        .def("getNeedXiaCaiYang", &XinTan::Frame::getNeedXiaCaiYang)
        .def("getXBinning", &XinTan::Frame::getXBinning)
        .def("getYBinning", &XinTan::Frame::getYBinning)
        .def("getOrgWidth", &XinTan::Frame::getOrgWidth)
        .def("getOrgHeight", &XinTan::Frame::getOrgHeight)
        .def("getHasPointCloud", &XinTan::Frame::getHasPointCloud)
        .def("getFrameLabel", &XinTan::Frame::getFrameLabel)

        // 设置方法
        .def("setFrameId", &XinTan::Frame::setFrameId)
        .def("setTimeStampS", &XinTan::Frame::setTimeStampS)
        .def("setDistdataIndex", &XinTan::Frame::setDistdataIndex)
        .def("setRefdataIndex", &XinTan::Frame::setRefdataIndex)
        .def("setAmpdataIndex", &XinTan::Frame::setAmpdataIndex)
        .def("setMotionIndex", &XinTan::Frame::setMotionIndex)
        .def("setDustPercent", &XinTan::Frame::setDustPercent);

    // Binding for CBEventData
    py::class_<XinTan::CBEventData, std::shared_ptr<XinTan::CBEventData>>(m, "CBEventData")
        .def(py::init<std::string, uint8_t, XinTan::XByteArray, uint8_t>(),
             py::arg("eventstr"), py::arg("cmdid"), py::arg("data"), py::arg("cmdstate"))
        .def_readwrite("eventstr", &XinTan::CBEventData::eventstr)
        .def_readwrite("cmdid", &XinTan::CBEventData::cmdid)
        .def_readwrite("cmdstate", &XinTan::CBEventData::cmdstate)
        .def_readwrite("data", &XinTan::CBEventData::data);

    // Binding for XtSdk
    py::class_<XinTan::XtSdk>(m, "XtSdk")
        .def(py::init<const std::string &, const std::string &, void *>(),
             py::arg("logpath") = "./xtlog/",
             py::arg("logtag") = "",
             py::arg("pxt") = nullptr,
             py::keep_alive<1, 4>())
        .def("setCallback",
             py::overload_cast<std::function<void(const std::shared_ptr<XinTan::CBEventData> &)>,
                               std::function<void(const std::shared_ptr<XinTan::Frame> &)>>(
                 &XinTan::XtSdk::setCallback),
             py::arg("eventcallback") = nullptr,
             py::arg("imgcallback") = nullptr,
             py::keep_alive<1, 2>(), // 保持 'eventcallback' 生命周期
             py::keep_alive<1, 3>()) // 保持 'imgcallback' 生命周期

        .def("setCallback",
             py::overload_cast<std::function<void(const std::shared_ptr<XinTan::CBEventData> &, void *)>,
                               std::function<void(const std::shared_ptr<XinTan::Frame> &, void *)>,
                               void *, void *>(
                 &XinTan::XtSdk::setCallback),
             py::arg("eventcallback"),
             py::arg("imgcallback"),
             py::arg("peventIn") = nullptr,
             py::arg("pimgIn") = nullptr,
             py::keep_alive<1, 2>(), // 保持 'eventcallback' 生命周期
             py::keep_alive<1, 3>(), // 保持 'imgcallback' 生命周期
             py::keep_alive<1, 4>(), // 保持 'peventIn' 生命周期
             py::keep_alive<1, 5>()) // 保持 'pimgIn' 生命周期
        .def("startup", &XinTan::XtSdk::startup)
        .def("shutdown", &XinTan::XtSdk::shutdown)
        .def("isconnect", &XinTan::XtSdk::isconnect)
        .def("getSdkState", &XinTan::XtSdk::getSdkState)
        .def("getDevState", &XinTan::XtSdk::getDevState)
        .def("getStateStr", &XinTan::XtSdk::getStateStr)
        .def("getfps", &XinTan::XtSdk::getfps)
        .def("setConnectIpaddress", &XinTan::XtSdk::setConnectIpaddress, py::arg("ipAddress"))
        .def("setConnectSerialportName", &XinTan::XtSdk::setConnectSerialportName, py::arg("serialportName"))
        .def("setSdkKalmanFilter", &XinTan::XtSdk::setSdkKalmanFilter, py::arg("factor"), py::arg("threshold"), py::arg("timedf") = 300)
        .def("setSdkEdgeFilter", &XinTan::XtSdk::setSdkEdgeFilter, py::arg("threshold"))
        .def("setSdkMedianFilter", &XinTan::XtSdk::setSdkMedianFilter, py::arg("size"))
        .def("setSdkDustFilter", &XinTan::XtSdk::setSdkDustFilter, py::arg("threshold"), py::arg("framecount") = 4, py::arg("timedf") = 300, py::arg("validpercent") = 100)
        .def("setSdkReflectiveFilter", &XinTan::XtSdk::setSdkReflectiveFilter, py::arg("threshold_min"), py::arg("threshold_max"))
        .def("setSdkAvgFilter", &XinTan::XtSdk::setSdkAvgFilter, py::arg("size"), py::arg("timedf") = 300)
        .def("clearAllSdkFilter", &XinTan::XtSdk::clearAllSdkFilter)
        .def("setTransMirror", &XinTan::XtSdk::setTransMirror, py::arg("hmirror"), py::arg("vmirror"))
        .def("setPointsCornerCut", &XinTan::XtSdk::setPointsCornerCut, py::arg("iscut"))
        .def("setPointsLsbCut", &XinTan::XtSdk::setPointsLsbCut, py::arg("minAmp"))
        .def("setDownSample", &XinTan::XtSdk::setDownSample, py::arg("x"), py::arg("y"))
        .def("setCutCorner", &XinTan::XtSdk::setCutCorner, py::arg("cutvalue"))
        .def("setSdkCloudCoordType", &XinTan::XtSdk::setSdkCloudCoordType, py::arg("type"))
        .def("setReflectivityCoef", &XinTan::XtSdk::setReflectivityCoef, py::arg("coef"))
        .def("setPostProcess", &XinTan::XtSdk::setPostProcess, py::arg("dilation_pixels"), py::arg("mode"), py::arg("winsize") = 9, py::arg("motion_size") = 5)
        .def("testDev", &XinTan::XtSdk::testDev)
        .def("resetDev", &XinTan::XtSdk::resetDev)
        .def("restoreDefault", &XinTan::XtSdk::restoreDefault)
        .def("start", &XinTan::XtSdk::start, py::arg("imgType"), py::arg("isOnce") = false)
        .def("stop", &XinTan::XtSdk::stop)

        .def("getDevInfo", [](XinTan::XtSdk &self) -> std::pair<bool, XinTan::RespDevInfo>
             {
                 XinTan::RespDevInfo devInfo;
                 bool result = self.getDevInfo(devInfo);
                 return std::make_pair(result, devInfo); })

        .def("getDevConfig", [](XinTan::XtSdk &self) -> std::pair<bool, XinTan::RespDevConfig>
             {
                 XinTan::RespDevConfig devCfg;
                 bool result = self.getDevConfig(devCfg);
                 return std::make_pair(result, devCfg); })

        .def("setIp", &XinTan::XtSdk::setIp, py::arg("ip"), py::arg("mask"), py::arg("gate"))
        .def("setUdpDestIp", &XinTan::XtSdk::setUdpDestIp, py::arg("ip"), py::arg("port") = 7687)
        .def("setFilter", &XinTan::XtSdk::setFilter, py::arg("temporal_factor"), py::arg("temporal_threshold"), py::arg("edgefilter_threshold"))
        .def("setIntTimesus", &XinTan::XtSdk::setIntTimesus, py::arg("timeGs"), py::arg("time1"), py::arg("time2"), py::arg("time3"), py::arg("time4"), py::arg("time5") = 0)
        .def("setMinAmplitude", &XinTan::XtSdk::setMinAmplitude, py::arg("minAmplitude"))
        .def("setHdrMode", &XinTan::XtSdk::setHdrMode, py::arg("mode"))
        .def("setModFreq", &XinTan::XtSdk::setModFreq, py::arg("freqType"))
        .def("setRoi", &XinTan::XtSdk::setRoi, py::arg("x0"), py::arg("y0"), py::arg("x1"), py::arg("y1"))
        .def("setMaxFps", &XinTan::XtSdk::setMaxFps, py::arg("maxfps"))
        .def("getLensCalidata", [](XinTan::XtSdk &self) -> std::pair<bool, XinTan::CamParameterS>
             {
                 XinTan::CamParameterS cam_para;
                 bool result = self.getLensCalidata(cam_para);
                 return std::make_pair(result, cam_para); })
        .def("customCmd", &XinTan::XtSdk::customCmd, py::arg("cmdId"), py::arg("data"), py::arg("respData"), py::arg("timeoutms") = 1000)
        .def("getLogtagName", &XinTan::XtSdk::getLogtagName)
        .def("doUdpFrameData", &XinTan::XtSdk::doUdpFrameData, py::arg("udpframeData"), py::arg("frame_label") = "")
        .def("doXbinFrameData", &XinTan::XtSdk::doXbinFrameData, py::arg("xbin_path"))
        .def("doXbinRecord", &XinTan::XtSdk::doXbinRecord, py::arg("xbin_path"), py::arg("frame"), py::arg("currIndex"))

        .def("updateFW", &XinTan::XtSdk::updateFW, py::arg("bin_path"))
        .def("setMultiModFreq", &XinTan::XtSdk::setMultiModFreq, py::arg("freqType1"), py::arg("freqType2"), py::arg("freqType3"), py::arg("freqType4"), py::arg("freqType5"))
        .def("setAdditionalGray", &XinTan::XtSdk::setAdditionalGray, py::arg("on"))
        .def("getImuExtParamters", &XinTan::XtSdk::getImuExtParamters, py::arg("imuparameters"), py::arg("flag") = 1)
        .def("setBinningV", &XinTan::XtSdk::setBinningV, py::arg("flag"));

    // Binding enums
    py::enum_<XinTan::SdkState>(m, "SdkState")
        .value("STATE_UNSTARTUP", XinTan::SdkState::STATE_UNSTARTUP)
        .value("STATE_PORTOPENING", XinTan::SdkState::STATE_PORTOPENING)
        .value("STATE_TXRX_VERIFYING", XinTan::SdkState::STATE_TXRX_VERIFYING)
        .value("STATE_UDPIMGOK", XinTan::SdkState::STATE_UDPIMGOK)
        .value("STATE_CONNECTED", XinTan::SdkState::STATE_CONNECTED)
        .value("STATE_UNKNOW", XinTan::SdkState::STATE_UNKNOW);

    py::enum_<XinTan::DevStateCode>(m, "DevStateCode")
        .value("DevSTATE_DISCONNECTED", XinTan::DevStateCode::DevSTATE_DISCONNECTED)
        .value("DevSTATE_INIT", XinTan::DevStateCode::DevSTATE_INIT)
        .value("DevSTATE_IDLE", XinTan::DevStateCode::DevSTATE_IDLE)
        .value("DevSTATE_STREAM", XinTan::DevStateCode::DevSTATE_STREAM)
        .value("DevSTATE_ERR_CSI", XinTan::DevStateCode::DevSTATE_ERR_CSI)
        .value("DevSTATE_ERR_I2C", XinTan::DevStateCode::DevSTATE_ERR_I2C)
        .value("DevSTATE_ERR_TEMPL", XinTan::DevStateCode::DevSTATE_ERR_TEMPL)
        .value("DevSTATE_WARN_TEMP", XinTan::DevStateCode::DevSTATE_WARN_TEMP)
        .value("DevSTATE_TEMP_DownFreq", XinTan::DevStateCode::DevSTATE_TEMP_DownFreq)
        .value("DevSTATE_ERR_TEMPH", XinTan::DevStateCode::DevSTATE_ERR_TEMPH)
        .value("DevSTATE_ERR_NOSENSOR", XinTan::DevStateCode::DevSTATE_ERR_NOSENSOR)
        .value("DevSTATE_ERR_NOMODIC", XinTan::DevStateCode::DevSTATE_ERR_NOMODIC)
        .value("DevSTATE_ERR_NOTEMPIC", XinTan::DevStateCode::DevSTATE_ERR_NOTEMPIC)
        .value("DevSTATE_ERR_BTNOAPP", XinTan::DevStateCode::DevSTATE_ERR_BTNOAPP)
        .value("DevSTATE_BOOTLOADER", XinTan::DevStateCode::DevSTATE_BOOTLOADER)
        .value("DevSTATE_ERR_UNKNOW", XinTan::DevStateCode::DevSTATE_ERR_UNKNOW)
        .value("DevSTATE_ERR_POWERV", XinTan::DevStateCode::DevSTATE_ERR_POWERV)
        .value("DevSTATE_ERR_POWERA", XinTan::DevStateCode::DevSTATE_ERR_POWERA)
        .value("DevSTATE_ERR_RAM", XinTan::DevStateCode::DevSTATE_ERR_RAM)
        .value("DevSTATE_ERR_FLASH", XinTan::DevStateCode::DevSTATE_ERR_FLASH)
        .value("DevSTATE_ERR_NOCALI", XinTan::DevStateCode::DevSTATE_ERR_NOCALI)
        .value("DevSTATE_ERR_NOIPARMS", XinTan::DevStateCode::DevSTATE_ERR_NOIPARMS)
        .value("DevSTATE_ERR_NOMAC", XinTan::DevStateCode::DevSTATE_ERR_NOMAC)
        .value("DevSTATE_ERR_NOSN", XinTan::DevStateCode::DevSTATE_ERR_NOSN)
        .value("DevSTATE_ERR_TEMPPARMS", XinTan::DevStateCode::DevSTATE_ERR_TEMPPARMS)
        .value("DevSTATE_CALIBRATING", XinTan::DevStateCode::DevSTATE_CALIBRATING)
        .value("DevSTATE_ERR_NOIMUPARAM", XinTan::DevStateCode::DevSTATE_ERR_NOIMUPARAM)
        .value("DevSTATE_ERR_MAX", XinTan::DevStateCode::DevSTATE_ERR_MAX);

    py::enum_<XinTan::CmdRespCode>(m, "CmdRespCode")
        .value("CmdResp_OK", XinTan::CmdRespCode::CmdResp_OK)
        .value("CmdResp_UNSUPPORT", XinTan::CmdRespCode::CmdResp_UNSUPPORT)
        .value("CmdResp_BUSY", XinTan::CmdRespCode::CmdResp_BUSY)
        .value("CmdResp_REJECT", XinTan::CmdRespCode::CmdResp_REJECT)
        .value("CmdResp_REPORT", XinTan::CmdRespCode::CmdResp_REPORT)
        .value("CmdResp_ERR_FORMAT", XinTan::CmdRespCode::CmdResp_ERR_FORMAT)
        .value("CmdResp_ERR_DATA", XinTan::CmdRespCode::CmdResp_ERR_DATA)
        .value("CmdResp_CSI", XinTan::CmdRespCode::CmdResp_CSI)
        .value("CmdResp_I2C", XinTan::CmdRespCode::CmdResp_I2C)
        .value("CmdResp_TEMPH", XinTan::CmdRespCode::CmdResp_TEMPH)
        .value("CmdResp_TEMPL", XinTan::CmdRespCode::CmdResp_TEMPL)
        .value("CmdResp_ERR_UNKNOW", XinTan::CmdRespCode::CmdResp_ERR_UNKNOW)
        .value("CmdResp_TIMEOUT", XinTan::CmdRespCode::CmdResp_TIMEOUT);

    py::enum_<XinTan::ImageType>(m, "ImageType")
        .value("IMG_DISTANCE", XinTan::ImageType::IMG_DISTANCE)
        .value("IMG_AMPLITUDE", XinTan::ImageType::IMG_AMPLITUDE)
        .value("IMG_GRAYSCALE", XinTan::ImageType::IMG_GRAYSCALE)
        .value("IMG_POINTCLOUD", XinTan::ImageType::IMG_POINTCLOUD)
        .value("IMG_POINTCLOUDAMP", XinTan::ImageType::IMG_POINTCLOUDAMP);

    m.def("get_image_type_strings", []()
          { return XinTan::ImageTypeStr; });

    py::enum_<XinTan::ModulationFreq>(m, "ModulationFreq")
        .value("FREQ_12M", XinTan::ModulationFreq::FREQ_12M)
        .value("FREQ_6M", XinTan::ModulationFreq::FREQ_6M)
        .value("FREQ_24M", XinTan::ModulationFreq::FREQ_24M)
        .value("FREQ_3M", XinTan::ModulationFreq::FREQ_3M)
        .value("FREQ_1_5M", XinTan::ModulationFreq::FREQ_1_5M)
        .value("FREQ_0_75M", XinTan::ModulationFreq::FREQ_0_75M);

    m.def("get_modulation_freq_strings", []()
          { return XinTan::ModulationFreqStr; });

    py::enum_<XinTan::HDRMode>(m, "HDRMode")
        .value("HDR_OFF", XinTan::HDRMode::HDR_OFF)
        .value("HDR_TAMPORAL", XinTan::HDRMode::HDR_TAMPORAL)
        .value("HDR_SPATIAL", XinTan::HDRMode::HDR_SPATIAL);
    m.def("get_modulation_HDRMode_strings", []()
          { return XinTan::HDRModeStr; });

    py::enum_<XinTan::ClOUDCOORD_TYPE>(m, "ClOUDCOORD_TYPE")
        .value("ClOUDCOORD_CAMERA", XinTan::ClOUDCOORD_TYPE::ClOUDCOORD_CAMERA)
        .value("ClOUDCOORD_CAR", XinTan::ClOUDCOORD_TYPE::ClOUDCOORD_CAR);

    // Binding for RespDevInfo
    py::class_<XinTan::RespDevInfo>(m, "RespDevInfo")
        .def(py::init<>())
        // .def_readwrite("ip", &XinTan::RespDevInfo::ip)
        // .def_readwrite("mac", &XinTan::RespDevInfo::mac)

        .def_property("ip", [](XinTan::RespDevInfo &self)
                      { return py::bytes(reinterpret_cast<const char *>(self.ip), 12); }, [](XinTan::RespDevInfo &self, const py::bytes &value)
                      {
                          auto bytes = value.cast<std::string>();
                          std::memcpy(self.ip, bytes.c_str(), std::min(bytes.size(), size_t(12))); })
        .def_property("mac", [](XinTan::RespDevInfo &self)
                      { return py::bytes(reinterpret_cast<const char *>(self.mac), 6); }, [](XinTan::RespDevInfo &self, const py::bytes &value)
                      {
                          auto bytes = value.cast<std::string>();
                          std::memcpy(self.mac, bytes.c_str(), std::min(bytes.size(), size_t(6))); })
        .def_property("isCalibrated", [](XinTan::RespDevInfo &self)
                      { return py::bytes(reinterpret_cast<const char *>(self.isCalibrated), 3); }, [](XinTan::RespDevInfo &self, const py::bytes &value)
                      {
                          auto bytes = value.cast<std::string>();
                          std::memcpy(self.isCalibrated, bytes.c_str(), std::min(bytes.size(), size_t(3))); })
        .def_property("udpDestIp", [](XinTan::RespDevInfo &self)
                      { return py::bytes(reinterpret_cast<const char *>(self.udpDestIp), 4); }, [](XinTan::RespDevInfo &self, const py::bytes &value)
                      {
                          auto bytes = value.cast<std::string>();
                          std::memcpy(self.udpDestIp, bytes.c_str(), std::min(bytes.size(), size_t(4))); })
        // 处理 uint16_t 数组为 py::array
        .def_property("chipid", [](XinTan::RespDevInfo &self)
                      { return py::array_t<uint16_t>(2, self.chipid); }, [](XinTan::RespDevInfo &self, const py::array_t<uint16_t> &value)
                      {
                          if (value.size() == 2) {
                              std::memcpy(self.chipid, value.data(), 2 * sizeof(uint16_t));
                          } })
        .def_readwrite("fwVersion", &XinTan::RespDevInfo::fwVersion)
        .def_readwrite("sn", &XinTan::RespDevInfo::sn)
        .def_readwrite("bootVersion", &XinTan::RespDevInfo::bootVersion)
        .def_readwrite("chipidStr", &XinTan::RespDevInfo::chipidStr)
        .def_readwrite("udpDestPort", &XinTan::RespDevInfo::udpDestPort)
        .def_readwrite("timeSyncType", &XinTan::RespDevInfo::timeSyncType)
        .def_readwrite("bdrnulens", &XinTan::RespDevInfo::bdrnulens);

    // Binding for RespDevConfig
    py::class_<XinTan::RespDevConfig>(m, "RespDevConfig")
        .def(py::init<>())
        .def_readwrite("imgType", &XinTan::RespDevConfig::imgType)
        .def_readwrite("modFreq", &XinTan::RespDevConfig::modFreq)
        .def_readwrite("hdrMode", &XinTan::RespDevConfig::hdrMode)
        .def_property("integrationTimes", [](XinTan::RespDevConfig &self)
                      { return py::array_t<uint16_t>(4, self.integrationTimes); }, [](XinTan::RespDevConfig &self, const py::array_t<uint16_t> &value)
                      {
                          if (value.size() == 4) {
                              std::memcpy(self.integrationTimes, value.data(), 4 * sizeof(uint16_t));
                          } })
        .def_readwrite("integrationTimeGs", &XinTan::RespDevConfig::integrationTimeGs)
        .def_readwrite("miniAmp", &XinTan::RespDevConfig::miniAmp)
        .def_readwrite("isFilterOn", &XinTan::RespDevConfig::isFilterOn)
        .def_property("roi", [](XinTan::RespDevConfig &self)
                      { return py::array_t<uint16_t>(4, self.roi); }, [](XinTan::RespDevConfig &self, const py::array_t<uint16_t> &value)
                      {
                          if (value.size() == 4) {
                              std::memcpy(self.roi, value.data(), 4 * sizeof(uint16_t));
                          } })
        .def_readwrite("maxfps", &XinTan::RespDevConfig::maxfps)
        .def_readwrite("bCompensateOn", &XinTan::RespDevConfig::bCompensateOn)
        .def_readwrite("bBinningH", &XinTan::RespDevConfig::bBinningH)
        .def_readwrite("bBinningV", &XinTan::RespDevConfig::bBinningV)
        .def_readwrite("freqChannel", &XinTan::RespDevConfig::freqChannel)
        .def_readwrite("setmaxfps", &XinTan::RespDevConfig::setmaxfps)
        .def_readwrite("vcsel", &XinTan::RespDevConfig::vcsel)
        .def_readwrite("ptpdomain", &XinTan::RespDevConfig::ptpdomain)
        .def_readwrite("endianType", &XinTan::RespDevConfig::endianType)
        .def_readwrite("version", &XinTan::RespDevConfig::version)
        .def_property("freq", [](XinTan::RespDevConfig &self)
                      { return py::bytes(reinterpret_cast<const char *>(self.freq), 4); }, [](XinTan::RespDevConfig &self, const py::bytes &value)
                      {
                          auto bytes = value.cast<std::string>();
                          std::memcpy(self.freq, bytes.c_str(), std::min(bytes.size(), size_t(4))); })
        .def_readwrite("bcut_filteron", &XinTan::RespDevConfig::bcut_filteron)
        .def_readwrite("cut_intgrttime0", &XinTan::RespDevConfig::cut_intgrttime0)
        .def_readwrite("cut_distance0", &XinTan::RespDevConfig::cut_distance0)
        .def_readwrite("cut_intgrttime1", &XinTan::RespDevConfig::cut_intgrttime1)
        .def_readwrite("cut_distance1", &XinTan::RespDevConfig::cut_distance1);
}

