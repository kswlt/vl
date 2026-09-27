#pragma once

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <array>
#include <ctime> // for time_t

namespace XinTan
{

#define DEPTH_ABNORMAL 964000
#define DEPTH_LOW 964001
#define DEPTH_IGNORE 964008
#define DEPTH_FILTED 964010
#define AMPLITUDE_ABNORMAL 64000
#define AMPLITUDE_LOW 64001
#define AMPLITUDE_ADC 64002
#define AMPLITUDE_OVEREXPOSURE 64003
#define AMPLITUDE_DEFECTIVE 64004
#define AMPLITUDE_INVALID 64007
#define AMPLITUDE_FILTED  64010

    typedef void (*LogFunction)(const char *pfunction, const char *plogstr);

    enum DataType
    {
        AMPLITUDE,
        DISTANCE,
        RESERVER,
        GRAYSCALE
    };

    typedef std::vector<uint8_t> XByteArray;

    struct XtPointXYZI
    {
        float x;
        float y;
        float z;
        float intensity;
    };

    struct CamParameterS
    {
        float fx;
        float fy;
        float cx;
        float cy;
        float k1;
        float k2;
        float k3;
        float p1;
        float p2;
    };

    struct ExtrinsicIMULidar
    {
        // std::array<std::array<double, 3>, 3> R_imu_lidar;
        // std::array<double, 3> t_imu_lidar;
        float qw;
        float qx;
        float qy;
        float qz;
        float tx;
        float ty;
        float tz;
    };

    enum IMAGE_FLAG
    {
        IMG_DIST = 0x01,
        IMG_AMP = 0x02,
        IMG_LEVEL = 0x04,
        IMG_GS16 = 0x08,
        IMG_DCS = 0x10,
        IMG_GS8 = 0x20,
        IMG_DCSN = 0x40
    };

    class IFrame
    {
    public:
        virtual ~IFrame() = default;
        enum DataType
        {
            AMPLITUDE,
            DISTANCE,
            RESERVER,
            GRAYSCALE
        };
        // 获取数据
        virtual const uint8_t getImageFlags() = 0;
        virtual const int getDistDataSize() = 0;
        virtual const uint32_t getDistData(const size_t &index) = 0;
        virtual const uint16_t getAmplData(const size_t &index) = 0;
        virtual const float getMaxAmplData() = 0;
        virtual const uint32_t getRawDistData(const size_t &index) = 0;
        virtual const uint16_t getGrayscaleData(const size_t &index) = 0;
        virtual const float getReflectivity(const size_t &index) = 0;
        virtual const uint8_t getleveldata(const size_t &index) = 0;
        virtual const uint8_t getMotionFlag(const size_t &index) = 0;
        virtual const uint8_t getFreqMap(const size_t &index) = 0;
        virtual const uint16_t getIntMap(const size_t &index) = 0;

        virtual uint32_t *getDistDataBuffer() = 0;
        virtual uint16_t *getAmplDataBuffer() = 0;
        virtual uint32_t *getRawDistDataBuffer() = 0;
        virtual uint16_t *getGrayscaleDataBuffer() = 0;
        virtual float *getReflectivityBuffer() = 0;
        virtual uint8_t *getleveldataBuffer() = 0;
        virtual uint8_t *getMotionFlagBuffer() = 0;
        virtual uint8_t *getFreqMapBuffer() = 0;
        virtual uint16_t *getIntMapBuffer() = 0;

        //  纯虚函数声明
        virtual uint8_t getFrameVersion() = 0;
        virtual uint16_t getPixelDataOffset() = 0;
        virtual uint64_t getFrameId() = 0;
        virtual uint16_t getDataType() = 0;
        virtual uint16_t getWidth() = 0;
        virtual uint16_t getHeight() = 0;
        virtual uint32_t getPxSize() = 0;
        virtual uint16_t getRoiX0() = 0;
        virtual uint16_t getRoiY0() = 0;
        virtual uint8_t getBinning() = 0;
        virtual uint64_t getTimeStampS() = 0;
        virtual uint32_t getTimeStampNS() = 0;
        virtual uint8_t getTimeStampState() = 0;
        virtual uint8_t getTimeStampType() = 0;
        virtual int16_t getTemperature() = 0;
        virtual int16_t getVcselTemperature() = 0;
        virtual int getDustPercent() = 0;
        virtual bool getNeedXiaCaiYang() = 0;
        virtual uint16_t getXBinning() = 0;
        virtual uint16_t getYBinning() = 0;
        virtual uint16_t getOrgWidth() = 0;
        virtual uint16_t getOrgHeight() = 0;
        virtual bool getHasPointCloud() = 0;

        virtual void setFrameId(uint64_t id) = 0;
        virtual void setTimeStampS(uint64_t ts) = 0;
        virtual void setDistdataIndex(const size_t &index, const uint32_t &data) = 0;
        virtual void setRefdataIndex(const size_t &index, const float &data) = 0;
        virtual void setAmpdataIndex(const size_t &index, const uint16_t &data) = 0;
        virtual void setMotionIndex(const size_t &index, const uint8_t &data) = 0;
        virtual void setDustPercent(int data) = 0;
        // 其他方法
        virtual void sortData(const XByteArray &xByteArray) = 0;
    };

} // namespace XinTan
