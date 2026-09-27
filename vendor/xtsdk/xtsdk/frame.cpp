/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#include "frame.h"
#include <cassert>
#include <iostream>
#include <string>
namespace XinTan
{

    uint16_t g_xbinning = 0;
    uint16_t g_ybinning = 0;

    float g_reflectcoef = 0.0;
    uint32_t g_reflectcutmax = 0.0;

    Frame::Frame(std::string &logtag_, uint16_t dataType_, uint64_t frame_id_,
                 uint16_t width_, uint16_t height_, uint16_t payloadOffset)
        : pixelDataOffset(payloadOffset), frame_id(frame_id_), dataType(dataType_),
          width(width_), height(height_),
          px_size(sizeof(uint16_t)), logtagname(logtag_)

    {
        hasPointcloud = false;

        frame_version = 0;
        roi_x0 = 0;
        roi_y0 = 0;
        binning = 0;
        dust_percent = -1;
        needxiacaiyang = true;

        orgwidth = width;
        orgheight = height;

        xbinning = 1;
        ybinning = 1;
        binning_div = 1.0f;

        if (g_xbinning > 1)
            xbinning = g_xbinning;

        if (g_ybinning > 1)
            ybinning = g_ybinning;

        frame_label = std::to_string(frame_id);

        m_refcof = {0.0,  // FREQ_12M
                    0.0,  // FREQ_6M
                    0.0,  // FREQ_24M
                    0.0,  // FREQ_3M
                    0.0,  // FREQ_1_5M
                    0.0,  // FREQ_0_75M
                    0.0}; // FREQ_4_8M

        devsdk_feq_map = {{0, 2},
                          {1, 0},
                          {3, 1},
                          {4, 6},
                          {7, 3},
                          {15, 4},
                          {31, 5}};

        refcof_S240MINI = {0.051779f,  // FREQ_12M
                           0.038790f,  // FREQ_6M
                           0.070588f,  // FREQ_24M
                           0.051976f,  // FREQ_3M
                           0.041449f,  // FREQ_1_5M
                           0.0399f,    // FREQ_0_75M
                           0.047257f}; // FREQ_4_8M

        refcof_S240PRO = {0.027421f,  // FREQ_12M
                          0.020683f,  // FREQ_6M
                          0.039181f,  // FREQ_24M
                          0.029102f,  // FREQ_3M
                          0.021447f,  // FREQ_1_5M
                          0.0198f,    // FREQ_0_75M
                          0.024667f}; // FREQ_4_8M

        refcof_M240 = {0.008084f,  // FREQ_12M
                       0.006265f,  // FREQ_6M
                       0.010401f,  // FREQ_24M
                       0.006837f,  // FREQ_3M
                       0.005828f,  // FREQ_1_5M
                       0.00795f,   // FREQ_0_75M
                       0.005828f}; // FREQ_4_8M

        refcof_M240MINI = {0.008084f * 2.0f,  // FREQ_12M
                           0.006265f * 2.0f,  // FREQ_6M
                           0.010401f * 2.0f,  // FREQ_24M
                           0.006837f * 2.0f,  // FREQ_3M
                           0.005828f * 2.0f,  // FREQ_1_5M
                           0.00795f * 2.0f,   // FREQ_0_75M
                           0.005828f * 2.0f}; // FREQ_4_8M

        refcof_M240PRO = {0.004703f * 2.0f,  // FREQ_12M
                          0.004804f * 2.0f,  // FREQ_6M
                          0.006233f * 2.0f,  // FREQ_24M
                          0.004220f * 2.0f,  // FREQ_3M
                          0.003947f * 2.0f,  // FREQ_1_5M
                          0.00436f * 2.0f,   // FREQ_0_75M
                          0.004463f * 2.0f}; // FREQ_4_8M

        refcof_M240MAX = {0.002572f,   // FREQ_12M
                          0.002509f,   // FREQ_6M
                          0.004244f,   // FREQ_24M
                          0.002066f,   // FREQ_3M
                          0.002140f,   // FREQ_1_5M
                          0.00104348f, // FREQ_0_75M
                          0.002406f};  // FREQ_4_8M

        refcof_M240ULTRA = {0.002282f * 4.0f,  // FREQ_12M
                            0.001671f * 4.0f,  // FREQ_6M
                            0.002710f * 4.0f,  // FREQ_24M
                            0.002112f * 4.0f,  // FREQ_3M
                            0.001516f * 4.0f,  // FREQ_1_5M
                            0.0019f * 4.0f,    // FREQ_0_75M
                            0.001614f * 4.0f}; // FREQ_4_8M

        refcof_M60 = {0.029626f,  // FREQ_12M
                      0.027954f,  // FREQ_6M
                      0.047552f,  // FREQ_24M
                      0.024505f,  // FREQ_3M
                      0.023817f,  // FREQ_1_5M
                      0.035275f,  // FREQ_0_75M
                      0.030925f}; // FREQ_4_8M
    }

    // const std::vector<uint8_t>& Frame::getFrameData()   { return frameData; }

    const uint8_t Frame::getImageFlags()
    {
        return info.imageflags;
    }
    const uint32_t Frame::getDistData(const size_t &index)
    {
        // XTLOGINFO("size inside: " + std::to_string(distData.size()));
        if (index < distData.size())
        {
            return distData[index];
        }
        return 0;
    }
    const int Frame::getDistDataSize()
    {
        return distData.size();
    }

    const uint16_t Frame::getAmplData(const size_t &index)
    {
        if (index < amplData.size())
        {
            return amplData[index];
        }
        return 0;
    }

    const uint32_t Frame::getRawDistData(const size_t &index)
    {
        if (index < rawdistData.size())
        {
            return rawdistData[index];
        }
        return 0;
    }
    const uint16_t Frame::getGrayscaleData(const size_t &index)
    {
        if (index < grayscaledata.size())
        {
            return grayscaledata[index];
        }
        return 0;
    }
    const float Frame::getReflectivity(const size_t &index)
    {
        if (index < reflectivity.size())
        {
            return reflectivity[index];
        }
        return 0.0;
    }
    const uint8_t Frame::getleveldata(const size_t &index)
    {
        if (index < leveldata.size())
        {
            return leveldata[index];
        }
        return 0;
    }

    const uint8_t Frame::getMotionFlag(const size_t &index)
    {
        // assert(index < sizeof(motion_vec)/sizeof(motion_vec[0]));
        // return motion_vec[index];
        if (index < motion_vec.size())
        {
            return motion_vec[index];
        }
        return 0;
    }
    const uint8_t Frame::getFreqMap(const size_t &index)
    {
        if (index < freqMap.size())
        {
            return freqMap[index];
        }
        return 0;
    }
    const uint16_t Frame::getIntMap(const size_t &index)
    {
        if (index < intMap.size())
        {
            return intMap[index];
        }
        return 0;
    }

    uint32_t *Frame::getDistDataBuffer()
    {
        return distData.data();
    }

    uint16_t *Frame::getAmplDataBuffer()
    {
        return amplData.data();
    }
    uint32_t *Frame::getRawDistDataBuffer()
    {
        return rawdistData.data();
    }
    uint16_t *Frame::Frame::getGrayscaleDataBuffer()
    {
        return grayscaledata.data();
    }
    float *Frame::getReflectivityBuffer()
    {
        return reflectivity.data();
    }
    uint8_t *Frame::getleveldataBuffer()
    {
        return leveldata.data();
    }
    uint8_t *Frame::getMotionFlagBuffer()
    {
        return motion_vec.data();
    }
    uint8_t *Frame::getFreqMapBuffer()
    {
        return freqMap.data();
    }
    uint16_t *Frame::getIntMapBuffer()
    {
        return intMap.data();
    }

    const float Frame::getMaxAmplData()
    {
        const int maxAmplitude = AMPLITUDE_ABNORMAL;
        auto it_amp = std::max_element(amplData.begin(), amplData.end(),
                                       [=](int a, int b)
                                       {
                                           return (a < maxAmplitude ? a : std::numeric_limits<int>::min()) <
                                                  (b < maxAmplitude ? b : std::numeric_limits<int>::min());
                                       });
        float maxamp = it_amp != amplData.end() && *it_amp < maxAmplitude ? *it_amp : maxAmplitude;
        return maxamp;
    }

    uint8_t Frame::getFrameVersion() { return frame_version; }
    uint16_t Frame::getPixelDataOffset() { return pixelDataOffset; }
    uint64_t Frame::getFrameId() { return frame_id; }
    uint16_t Frame::getDataType() { return dataType; }
    uint16_t Frame::getWidth() { return width; }
    uint16_t Frame::getHeight() { return height; }
    uint32_t Frame::getPxSize() { return px_size; }
    uint16_t Frame::getRoiX0() { return roi_x0; }
    uint16_t Frame::getRoiY0() { return roi_y0; }
    uint8_t Frame::getBinning() { return binning; }
    uint64_t Frame::getTimeStampS() { return timeStampS; }
    uint32_t Frame::getTimeStampNS() { return timeStampNS; }
    uint8_t Frame::getTimeStampState() { return timeStampState; }
    uint8_t Frame::getTimeStampType() { return timeStampType; }
    int16_t Frame::getTemperature() { return temperature; }
    int16_t Frame::getVcselTemperature() { return vcseltemperature; }
    int Frame::getDustPercent() { return dust_percent; }
    bool Frame::getNeedXiaCaiYang() { return needxiacaiyang; }
    uint16_t Frame::getXBinning() { return xbinning; }
    uint16_t Frame::getYBinning() { return ybinning; }
    uint16_t Frame::getOrgWidth() { return orgwidth; }
    uint16_t Frame::getOrgHeight() { return orgheight; }
    bool Frame::getHasPointCloud() { return hasPointcloud; }
    std::string Frame::getFrameLabel() { return frame_label; }

    void Frame::setFrameId(uint64_t id) { frame_id = id; }
    void Frame::setTimeStampS(uint64_t ts) { timeStampS = ts; }
    void Frame::setDistdataIndex(const size_t &index, const uint32_t &data)
    {
        if (index < distData.size())
        {
            distData[index] = data;
        }
    };
    void Frame::setRefdataIndex(const size_t &index, const float &data)
    {
        if (index < reflectivity.size())
        {
            reflectivity[index] = data;
        }
    };

    void Frame::setAmpdataIndex(const size_t &index, const uint16_t &data)
    {
        if (index < amplData.size())
        {
            amplData[index] = data;
        }
    }
    void Frame::setMotionIndex(const size_t &index, const uint8_t &data)
    {
        if (index < amplData.size())
        {
            motion_vec[index] = data;
        }
    }

    void Frame::setDustPercent(int data) { dust_percent = data; };

    void Frame::resetData()
    {
        px_size = sizeof(uint16_t);

        rawdistData.resize(width * height);
        distData.resize(width * height);
        amplData.resize(width * height);
        reflectivity.resize(width * height);
        grayscaledata.resize(width * height);
        freqMap.resize(width * height);
        intMap.resize(width * height);
        motion_vec.assign(width * height, 0);
    }

    bool bis_m60 = false;
    void Frame::setCofArray()
    {
        std::string sn_str(reinterpret_cast<char *>(info.sn), 29);
        std::string sn_str_without_nulls;
        sn_str_without_nulls.reserve(29); // Reserve space for efficiency

        for (char c : sn_str)
        {
            if (c != '\x00')
            {
                sn_str_without_nulls.push_back(c);
            }
        }
        bis_m60 = false;

        if ((int)sn_str_without_nulls.find("S240MINI") > 0)
            m_refcof = refcof_S240MINI;
        else if ((int)sn_str_without_nulls.find("S240PRO") > 0)
            m_refcof = refcof_S240PRO;
        else if ((int)sn_str_without_nulls.find("S120MINI") > 0)
            m_refcof = refcof_S240MINI;
        else if ((int)sn_str_without_nulls.find("S120PRO") > 0)
            m_refcof = refcof_S240PRO;
        else if ((int)sn_str_without_nulls.find("M120MINI") > 0)
            m_refcof = refcof_M240MINI;
        else if ((int)sn_str_without_nulls.find("M120MAX") > 0)
            m_refcof = refcof_M240MAX;
        else if ((int)sn_str_without_nulls.find("M120PRO") > 0)
            m_refcof = refcof_M240PRO;
        else if ((int)sn_str_without_nulls.find("M120ULTRA") > 0)
            m_refcof = refcof_M240ULTRA;
        else if ((int)sn_str_without_nulls.find("M60") > 0)
        {
            m_refcof = refcof_M60;
            bis_m60 = true;
        }
        else if ((int)sn_str_without_nulls.find("Z240") > 0)
            m_refcof = refcof_M240;
        else if ((int)sn_str_without_nulls.find("M240") > 0)
            m_refcof = refcof_M240;
        else
            m_refcof = refcof_S240MINI;
    }

    void Frame::sortData(const XByteArray &data)
    {
        int i, mirrori;
        uint32_t dist_unitmm = 1;
        int offset, dist_offset, gs16_offset, level_offset, dcs_offset;

        int pixelcount = orgwidth * orgheight;

        setCofArray();

        // V3 check
        if ((frame_version == 3) && (info.magicToken == 0x33CCAA50))
        {
            dist_unitmm = info.unit_div;
            timeStampType = info.timesync_type;
            timeStampState = info.timesync_state;
        }
        else
        {

            if (dataType == Frame::AMPLITUDE)
                info.imageflags = (IMG_DIST | IMG_AMP);
            else if (dataType == Frame::DISTANCE)
                info.imageflags = IMG_DIST;
            else if (dataType == Frame::GRAYSCALE)
                info.imageflags = IMG_GS16;
            else
                info.imageflags = 0;
        }

        // check size
        int wantdatasize = 0;
        dist_offset = pixelDataOffset;
        gs16_offset = dist_offset;
        level_offset = dist_offset;
        dcs_offset = dist_offset;

        if (info.imageflags & IMG_DIST)
            wantdatasize += pixelcount * 2;
        if (info.imageflags & IMG_AMP)
            wantdatasize += pixelcount * 2;
        if (info.imageflags & IMG_GS16)
        {
            gs16_offset = dist_offset + wantdatasize;
            wantdatasize += pixelcount * 2;
        }
        if (info.imageflags & IMG_LEVEL)
        {
            level_offset = dist_offset + wantdatasize;
            wantdatasize += pixelcount / 2;
        }
        if (info.imageflags & IMG_DCS)
        {
            dcs_offset = dist_offset + wantdatasize;
            wantdatasize += pixelcount * 2 * 4;
        }
        if (data.size() < (wantdatasize + 56))
        {
            info.imageflags = 0;
            std::cout << "framesize error " + std::to_string(wantdatasize) << " " + std::to_string(data.size()) << std::endl;
            return;
        }

        // 提取距离
        if (info.imageflags & IMG_DIST)
        {
            int pixelbytes = 2;
            if (info.imageflags & IMG_AMP)
                pixelbytes = 4;

            for (i = 0; i < pixelcount; i++)
            {
                offset = dist_offset + i * pixelbytes;
                mirrori = (i / orgwidth) * orgwidth + orgwidth - 1 - i % orgwidth;
                if(bis_m60)
                    mirrori = i;

                distData[mirrori] = data[offset + 1] << 8 | data[offset];

                if (distData[mirrori] < 64000)
                {
                    if (dist_unitmm > 1) // 距离单位处理
                        distData[mirrori] *= dist_unitmm;
                }
                else
                    distData[mirrori] += 900000;
            }
        }

        // 提取强度
        if (info.imageflags & IMG_AMP)
        {
            int pixelbytes = 2;
            if (info.imageflags & IMG_DIST)
                pixelbytes = 4;

            for (i = 0; i < pixelcount; i++)
            {
                offset = dist_offset + i * pixelbytes + 2;
                mirrori = (i / orgwidth) * orgwidth + orgwidth - 1 - i % orgwidth;
                if(bis_m60)
                    mirrori = i;

                amplData[mirrori] = data[offset + 1] << 8 | data[offset];
            }
        }

        // 提取灰度
        if (info.imageflags & IMG_GS16)
        {
            grayscaledata.resize(width * height);

            for (i = 0; i < pixelcount; i++)
            {
                offset = gs16_offset + i * 2;
                mirrori = (i / orgwidth) * orgwidth + orgwidth - 1 - i % orgwidth;
                if(bis_m60)
                    mirrori = i;

                grayscaledata[mirrori] = data[offset + 1] << 8 | data[offset];
            }
        }

        // 提取leveldata
        if (info.imageflags & IMG_LEVEL)
        {
            leveldata.resize(width * height);
            for (i = 0; i < pixelcount; i++)
            {
                if (i % 2 == 0)
                {
                    offset = level_offset + i / 2;

                    mirrori = (i / orgwidth) * orgwidth + orgwidth - 1 - i % orgwidth;
                    if(bis_m60)
                        mirrori = i;
                    leveldata[mirrori] = data[offset] & 0x0F;
                    mirrori = ((i + 1) / orgwidth) * orgwidth + orgwidth - 1 - (i + 1) % orgwidth;
                    if(bis_m60)
                        mirrori = i+1;
                    leveldata[mirrori] = (data[offset] >> 4) & 0x0F;
                }
            }
        }

        if (info.imageflags & IMG_DCS)
        {
            dcsdata.resize(pixelcount * 4);
            int dcssize = pixelcount * 4;
            for (i = 0; i < dcssize; i++)
            {
                offset = dcs_offset + i * 2;

                dcsdata[i] = data[offset + 1] << 8 | data[offset];
            }
        }

        if (info.binning & 0x01)
            binning_div *= 2;
        if (info.binning & 0x02)
            binning_div *= 2;

        // 下采样处理
        if (needxiacaiyang)
        {
            needxiacaiyang = false;

            if (xbinning > 1)
            {
                width = width / xbinning;
                binning_div *= 2.0f;
            }

            if (ybinning > 1)
            {
                height = height / ybinning;
                binning_div *= 2.0f;
            }
        }

        // 计算反射率
        //  if((g_reflectcoef > 0.0) && (info.imageflags & IMG_AMP))
        if (info.imageflags & IMG_AMP)
        {
            uint32_t ampvalue = 0;
            float dist2value = 0;
            float reflctValue = 0;
            uint8_t levelfreq = 0;
            uint16_t levelintegtime = 0;
            for (i = 0; i < pixelcount; i++)
            {
                ampvalue = amplData[i];
                dist2value = distData[i];

                if ((ampvalue < 64000) && (dist2value < 964000) && (dist2value > 0))
                {
                    float reflectcoef = m_refcof[0] / binning_div;
                    if (frame_version > 2)
                    {
                        uint32_t amplitude32 = ampvalue;
                        levelintegtime = info.integtime[0];
                        if (info.imageflags & IMG_LEVEL) //
                        {
                            if (info.integtime[leveldata[i]] > 0)
                            {
                                levelintegtime = info.integtime[leveldata[i]];
                                // levelfreq = info.freq[leveldata[i]];
                                levelfreq = devsdk_feq_map.at(info.freq[leveldata[i]]);
                            }
                        }
                        if (levelintegtime != 0)
                        {
                            amplitude32 = (amplitude32 * 200) / levelintegtime;
                        }
                        else
                        {
                            amplitude32 = 0.0;
                        }

                        reflectcoef = levelfreq >= m_refcof.size() ? reflectcoef : m_refcof[levelfreq] / binning_div;

                        ampvalue = amplitude32;
                    }

                    dist2value = dist2value / 1000;       // 转换为单位米
                    dist2value = dist2value * dist2value; // 距离的平方
                    reflctValue = (ampvalue * reflectcoef) * dist2value;
                    // reflctValue = (ampvalue * g_reflectcoef) * dist2value;

                    if (info.imageflags & IMG_LEVEL) // 反向纠正
                    {
                        if ((dist_unitmm > 3) && (distData[i] < 7500) && (info.integtime[leveldata[i]] > 1800)) // 130米内反向处理
                        {
                            if ((reflctValue < 0.5) && (amplData[i] < 200))
                            {
                                distData[i] += 25000; // 加100米

                                dist2value = distData[i];
                                dist2value = dist2value / 1000;       // 转换为单位米
                                dist2value = dist2value * dist2value; // 距离的平方
                                reflctValue = (ampvalue * reflectcoef) * dist2value;
                                // reflctValue = (ampvalue * g_reflectcoef) * dist2value;
                            }
                        }
                    }

                    reflectivity[i] = reflctValue;
                    freqMap[i] = levelfreq;
                    intMap[i] = levelintegtime;

                    if((reflctValue > g_reflectcutmax) && ( g_reflectcutmax > 0))
                    {
                        distData[i] = DEPTH_FILTED;
                        //amplData[i] = AMPLITUDE_FILTED;
                    }
                }
                else
                {
                    reflectivity[i] = 0.0;
                    freqMap[i] = 0;
                    intMap[i] = 0;
                }
            }
        }

        int xiapos = 0;
        if ((xbinning + ybinning) > 2)
        {
            for (uint16_t h = 0; h < height; h++)
            {
                for (uint16_t j = 0; j < width; j++)
                {

                    uint32_t pos = h * ybinning * orgwidth + j * xbinning;

                    int youxiadistcount = 0;
                    int youxiaampcount = 0;
                    uint32_t sumdist = 0;
                    uint32_t sumamp = 0;
                    float sumreflectivity = 0;
                    int leveli = i;

                    for (int y = 0; y < ybinning; y++)
                        for (int x = 0; x < xbinning; x++)
                        {
                            if (distData[pos + y * orgwidth + x] < 964000)
                            {
                                sumdist += distData[pos + y * orgwidth + x];
                                youxiadistcount++;
                                sumreflectivity += reflectivity[pos + y * orgwidth + x];

                                leveli = pos + y * orgwidth + x;
                            }
                            if (amplData[pos + y * orgwidth + x] < 3000)
                            {
                                sumamp += amplData[pos + y * orgwidth + x];
                                youxiaampcount++;
                            }
                        }

                    if (youxiadistcount > 0)
                    {
                        sumdist = sumdist / youxiadistcount;
                        sumreflectivity = sumreflectivity / youxiadistcount;
                    }
                    else
                    {
                        sumdist = distData[pos];
                        sumreflectivity = reflectivity[pos];
                    }

                    if (youxiaampcount > 0)
                        sumamp = sumamp / youxiaampcount;
                    else
                        sumamp = amplData[pos];

                    distData[xiapos] = leveli;

                    distData[xiapos] = sumdist;
                    amplData[xiapos] = sumamp;
                    reflectivity[xiapos] = sumreflectivity;

                    xiapos++;
                }
            }
        }

        rawdistData = distData;
    }

} // end namespace XinTan
