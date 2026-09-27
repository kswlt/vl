/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once

#include <string>
#include <ctime>
#include <cstdint>
#include "Iframe.h"
namespace XinTan
{

    enum Endian_TYPE
    {
        Endian_Little = 0x00,
        Endian_Big = 0x01
    };

    class Utils
    {

    public:
        static bool ipIsValid(std::string ipaddress);
        static bool isMacValid(std::string macaddress);
        static int macstr_parse(const char *point, uint8_t result[6]);
        static int ipstr_parse(const char *point, uint8_t result[6]);

        static bool isComport(std::string ipaddress);

        static time_t getTimeStamp();
        static std::string getTimeStr();

        static std::string getCurrentProgramDir();

        static uint16_t getValueUint16Endian(const uint8_t *buffer, uint8_t endian);
        static uint32_t getValueUint32Endian(const uint8_t *buffer, uint8_t endian);
        static uint64_t getValueUint48Endian(const uint8_t *buffer, uint8_t endian);
        static uint64_t getValueUint64Endian(const uint8_t *buffer, uint8_t endian);

        static void setValueUint16Endian(uint8_t *buffer, const uint16_t value, uint8_t endian);
        static void setValueUint32Endian(uint8_t *buffer, const uint32_t value, uint8_t endian);
        static void setValueUint64Endian(uint8_t *buffer, const uint64_t value, uint8_t endian);

        static bool hasExtension(const std::string &filePath, const std::string &extension);
        static void quaternionMultiply(const float *q1, const float *q2, float *result);
        static std::string getHostIp();
    };

} // end namespace XinTan
