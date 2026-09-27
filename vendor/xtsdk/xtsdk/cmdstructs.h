/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include <cstdint>

namespace XinTan {

//输出frame的附加信息
#pragma pack(push, 1)  // 设置字节对齐为 1
struct FrameInfo_t{
    uint32_t magicToken;//0x33CCAA50
    uint8_t  endianType;
    uint8_t  sn[29];
    uint8_t  fw[19];
    uint8_t  lensparameters[40];

    //配置
    uint8_t  imageflags;// bit0:dist b1:amp b2:level b3:gs16bit b4:dcs  b5:gs8bit
    uint8_t  hdrmode;
    uint8_t  levelused_bit;
    uint8_t  freq[5];
    uint16_t integtime[5];
    uint16_t integtimegs;
    uint16_t roix[2];
    uint16_t roiy[2];
    uint16_t miniAmp;
    uint8_t  channel;
    uint8_t  binning;
    uint8_t  reduce;
    uint8_t  vcsel;
    uint8_t  fps;
    uint8_t  aldcslevel;
    uint8_t  is2dcsFlag;//每当一个bit
    uint8_t  reserver1[6];

    //信息
    uint8_t  unit_div;//全局的距离单位: 默认50米量程是1mm,     100米量程为2mm，  200米量程为4mm
    uint16_t width;
    uint16_t height;
    int16_t temperature[2];//0是sensor温度， 1是灯板温度
    uint8_t  timesync_type;//0 tick, 1 //PPS+GPS , 2 ptp
    uint8_t  timesync_state;//0 丢失， 1: 在线
    uint64_t timestamp[5];//各档位的时间戳
    int32_t  ptpoffsetus;
    uint8_t  imusize;
    uint8_t  otherflags;
    uint8_t  chiptype;
    uint8_t  reserver2[3];
    uint8_t  version;

    uint32_t crc32;
    uint16_t infosize;
    uint8_t  devstate;
    uint8_t  ptclversion;
};
#pragma pack(pop)  // 恢复原始的对齐方式



//设备信息结构
#pragma pack(push, 1)  // 设置字节对齐为 1
struct DevInfo_t{
    uint32_t    magicToken;//0x33CCAA51
    uint16_t    size;
    uint8_t     version;
    uint8_t     sn[28];
    uint8_t     fw[18];
    uint8_t     bootver[18];
    uint8_t     ip[12];
    uint8_t     mac[6];
    uint16_t    chipid;
    uint16_t    waferid;
    uint32_t    udpDestIp;
    uint16_t    udpDestPort;
    uint8_t     timesync_type;
    uint8_t     timesync_state;
    uint8_t     errorcode;
    uint8_t     calibrated;//0 没标定， 1 部分标定， 2: 都有标定 ， 3： 多标定
    uint32_t    poweronCount;//上电次数
    uint32_t    runTimes;//运行时长 小时
    uint32_t    illumTimes;//发光时长 分钟 每累计30分钟存一次
    uint8_t     otherflags;//0：使用标记， bit 1、2、3: DRNU (0 没标定， 1 部分标定， 2: 都有标定 ， 3： 多标定, 4 error)
                                //bit4， 是否带镜头标定，bit5：sunlight , bit6:有imu
    uint8_t     reserver[9];
};
#pragma pack(pop)  // 恢复原始的对齐方式


//设备配置结构: 和poweron配置一致
#pragma pack(push, 1)  // 设置字节对齐为 1
struct DevCfg_t{
    uint32_t    magicToken;//0x33CCAA52
    uint16_t    size;
    uint8_t     version;
    uint8_t     endiantype;
    uint8_t     imageflags;// bit0:dist b1:amp b2:level b3:gs16bit b4:dcs  b5:gs8bit
    uint8_t     hdrmode;
    uint8_t     freq[5];
    uint16_t    integtime[5];
    uint16_t    integtimegs;
    uint16_t    roix[2];
    uint16_t    roiy[2];
    uint16_t    miniAmp;
    uint8_t     channel;
    uint8_t     binning;
    uint8_t     reduce;
    uint8_t     setfps;
    uint8_t     usefps;
    uint8_t     vcsel;
    uint8_t     timesync_type;//0 auto
    uint8_t     ptpdomain;
    uint8_t     bautorun;
    uint8_t     bdhcp;
    uint8_t     bcut_filteron;
    uint16_t    cutIntDist[3][2];
    uint8_t     aldcslevel;
    uint8_t     is2dcsFlag;//每当一个bit
    uint8_t     reserver[16];
};
#pragma pack(pop)  // 恢复原始的对齐方式

//输出frame的IMU附加信息
#pragma pack(push, 1)  // 设置字节对齐为 1
struct FrameOutImu_t{
    uint16_t mark;//0x55AA
    uint16_t flags;
    uint32_t data[22];//9+1+2+9
};
#pragma pack(pop)  // 恢复原始的对齐方式


} //end namespace XinTan
