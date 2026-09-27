/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once

#include "Iframe.h"
#include "cmdstructs.h"
#include "xtlogger.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstring>

namespace XinTan
{

    class Frame : public IFrame
    {
    public:
        Frame(std::string &logtag_, uint16_t dataType_, uint64_t frame_id_, uint16_t width_, uint16_t height_, uint16_t payloadOffset);

        FrameInfo_t info;
        FrameOutImu_t imu;

        uint8_t frame_version;
        uint16_t pixelDataOffset;

        uint64_t frame_id;
        uint16_t dataType; // frame数据类型
        uint16_t width;
        uint16_t height;
        uint32_t px_size;
        uint16_t roi_x0;
        uint16_t roi_y0;
        uint8_t binning;

        uint64_t timeStampS;    // 时间秒
        uint32_t timeStampNS;   // 纳秒
        uint8_t timeStampState; // 时间同步状态
        uint8_t timeStampType;  // 时间同步类型

        int16_t temperature;      // 温度
        int16_t vcseltemperature; // 灯板温度

        int dust_percent; // 0~100为有效，负数为无效

        bool needxiacaiyang; // 下采样用
        uint16_t xbinning;   // 下采样用
        uint16_t ybinning;   // 下采样用
        uint16_t orgwidth;   // 下采样用
        uint16_t orgheight;  // 下采样用

        bool hasPointcloud; // 是否有点云数据

        std::array<float, 7> m_refcof;

        std::unordered_map<uint8_t, uint8_t> devsdk_feq_map;
        std::array<float, 7> refcof_S240MINI;
        std::array<float, 7> refcof_S240PRO;
        std::array<float, 7> refcof_M240;
        std::array<float, 7> refcof_M240MINI;
        std::array<float, 7> refcof_M240PRO;
        std::array<float, 7> refcof_M240MAX;
        std::array<float, 7> refcof_M240ULTRA;
        std::array<float, 7> refcof_M60;

        std::vector<uint8_t> frameData; // 原始数据
        std::vector<uint8_t> leveldata; // 每个像素对应的level
        std::vector<uint16_t> dcsdata;  // 每个像素对应的level

        std::vector<uint32_t> rawdistData;   // 排序后的距离数据
        std::vector<uint32_t> distData;      // 排序后的距离数据
        std::vector<uint16_t> amplData;      // 排序后的信号强度数据
        std::vector<uint16_t> grayscaledata; // 额外的灰度数据
        // std::vector<uint16_t> reflectivity; //反射率数据，通过距离和信号强度计算出的
        std::vector<float> reflectivity;
        std::vector<uint8_t> freqMap;
        std::vector<uint16_t> intMap;
        std::vector<XtPointXYZI> points; // 点云数据，无序点云
        std::string &logtagname;
        std::string frame_label;
        // uint8_t motion_vec[320*240];
        std::vector<uint8_t> motion_vec;
        float binning_div;

        void sortData(const XByteArray &);
        void resetData();
        void setCofArray();

        const uint8_t getImageFlags();
        const int getDistDataSize();
        const uint32_t getDistData(const size_t &index);
        const uint16_t getAmplData(const size_t &index);
        const float getMaxAmplData();
        const uint32_t getRawDistData(const size_t &index);
        const uint16_t getGrayscaleData(const size_t &index);
        const float getReflectivity(const size_t &index);
        const uint8_t getleveldata(const size_t &index);
        const uint8_t getMotionFlag(const size_t &index);
        const uint8_t getFreqMap(const size_t &index);
        const uint16_t getIntMap(const size_t &index);

        uint32_t *getDistDataBuffer();
        uint16_t *getAmplDataBuffer();
        uint32_t *getRawDistDataBuffer();
        uint16_t *getGrayscaleDataBuffer();
        float *getReflectivityBuffer();
        uint8_t *getleveldataBuffer();
        uint8_t *getMotionFlagBuffer();
        uint8_t *getFreqMapBuffer();
        uint16_t *getIntMapBuffer();

        const time_t *getTimeAlg();
        // 纯虚函数声明
        uint8_t getFrameVersion();
        uint16_t getPixelDataOffset();
        uint64_t getFrameId();
        uint16_t getDataType();
        uint16_t getWidth();
        uint16_t getHeight();
        uint32_t getPxSize();
        uint16_t getRoiX0();
        uint16_t getRoiY0();
        uint8_t getBinning();
        uint64_t getTimeStampS();
        uint32_t getTimeStampNS();
        uint8_t getTimeStampState();
        uint8_t getTimeStampType();
        int16_t getTemperature();
        int16_t getVcselTemperature();
        int getDustPercent();
        bool getNeedXiaCaiYang();
        uint16_t getXBinning();
        uint16_t getYBinning();
        uint16_t getOrgWidth();
        uint16_t getOrgHeight();
        bool getHasPointCloud();
        std::string getFrameLabel();

        // 设置属性的方法可以根据需要添加，这里只添加几个示例
        void setFrameId(uint64_t id);
        void setTimeStampS(uint64_t ts);
        void setDistdataIndex(const size_t &index, const uint32_t &data);
        void setRefdataIndex(const size_t &index, const float &data);
        void setAmpdataIndex(const size_t &index, const uint16_t &data);
        void setMotionIndex(const size_t &index, const uint8_t &data);
        void setDustPercent(int data);
    };

} // end namespace XinTan
