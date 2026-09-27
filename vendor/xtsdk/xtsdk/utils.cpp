/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#include "utils.h"
#include <regex>
#include <chrono>
#include <stdio.h>
#include <iostream>

#include <boost/asio.hpp>
using boost::asio::ip::tcp;

#ifdef WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace XinTan
{

    bool Utils::ipIsValid(std::string ipaddress)
    {

        std::regex pattern("((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)");
        std::smatch res;
        if (regex_match(ipaddress, res, pattern))
        {
            return true;
        }
        return false;
    }
    bool Utils::isMacValid(std::string macaddress)
    {

        // std::regex pattern("(^[A-Fa-f\\d]{2}\\:[A-Fa-f\\d]{2}\\:[A-Fa-f\\d]{2}\\:[A-Fa-f\\d]{2}\\:[A-Fa-f\\d]{2}\\:[A-Fa-f\\d]{2}$)");
        std::regex pattern("(^[A-Fa-f\\d]{2}\\-[A-Fa-f\\d]{2}\\-[A-Fa-f\\d]{2}\\-[A-Fa-f\\d]{2}\\-[A-Fa-f\\d]{2}\\-[A-Fa-f\\d]{2}$)");

        std::smatch res;
        if (regex_match(macaddress, res, pattern))
        {
            return true;
        }
        return false;
    }

    int Utils::macstr_parse(const char *point, uint8_t result[6])
    {
        for (int i = 0; i < 6; i++)
        {
            result[i] = 0xfe;
        }
        char buf[18] = {0};
        int p = 0, q = 0;
        strcpy(buf, point);
        buf[strlen(point)] = '-';
        for (int i = 0; i < 6; i++)
        {
            // q = strchr(buf+p, '-') - buf;
            q = static_cast<int>(strchr(buf + p, '-') - buf);
            buf[q] = '\0';
            result[i] = static_cast<uint8_t>(strtol(buf + p, NULL, 16)); //(uint8_t)strtol(buf+p, NULL, 16);
            p = q + 1;
        }
        return 1;
    }

    int Utils::ipstr_parse(const char *point, uint8_t result[4])
    {
        for (int i = 0; i < 4; i++)
        {
            result[i] = 1;
        }
        char buf[18] = {0};
        int p = 0, q = 0;
        strcpy(buf, point);
        buf[strlen(point)] = '.';
        for (int i = 0; i < 4; i++)
        {
            // q = strchr(buf+p, '.') - buf;
            q = static_cast<int>(strchr(buf + p, '.') - buf);
            buf[q] = '\0';
            result[i] = static_cast<uint8_t>(strtol(buf + p, NULL, 10)); // strtol(buf+p, NULL, 10);
            p = q + 1;
        }
        return 1;
    }

    bool Utils::isComport(std::string ipaddress)
    {

        std::regex pattern("^[COM0-9]+$");
        std::regex patternlinux("^[/dev/ttyACM0-9]+$");
        std::smatch res;
        if (regex_match(ipaddress, res, pattern))
        {
            return true;
        }
        else if (regex_match(ipaddress, res, patternlinux))
        {
            return true;
        }
        return false;
    }

    bool Utils::hasExtension(const std::string &filePath, const std::string &extension)
    {
        // Check if the filePath ends with the extension
        if (filePath.length() >= extension.length())
        {
            return (0 == filePath.compare(filePath.length() - extension.length(), extension.length(), extension));
        }
        return false;
    }

    time_t Utils::getTimeStamp()
    {
        std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> tp = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        auto tmp = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
        time_t timestamp = tmp.count();
        // std::time_t timestamp = std::chrono::system_clock::to_time_t(tp);
        return timestamp;
    }

    std::string Utils::getTimeStr()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);

        auto localTm = localtime(&t);
        // struct tm localTm;
        // localtime_s(&localTm, &t);

        char buff[32];
        strftime(buff, 32, "%Y%m%d-%H-%M-%S", localTm);
        // strftime(buff, 32, "%Y%m%d-%H-%M-%S", &localTm);
        return std::string(buff);
    }

    uint16_t Utils::getValueUint16Endian(const uint8_t *buffer, uint8_t endian)
    {
        if (endian == Endian_Little)
        {
            uint16_t value = (buffer[1] << 8) | buffer[0];
            return value;
        }
        else
        {
            uint16_t value = (buffer[0] << 8) | buffer[1];
            return value;
        }
    }

    uint32_t Utils::getValueUint32Endian(const uint8_t *buffer, uint8_t endian)
    {
        if (endian == Endian_Little)
        {
            uint32_t value = (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
            return value;
        }
        else
        {
            uint32_t value = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
            return value;
        }
    }

    uint64_t Utils::getValueUint48Endian(const uint8_t *buffer, uint8_t endian)
    {
        if (endian == Endian_Little)
        {
            uint64_t value = ((uint64_t)buffer[5] << 40) | ((uint64_t)buffer[4] << 32) | (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
            return value;
        }
        else
        {
            uint64_t value = ((uint64_t)buffer[0] << 40) | ((uint64_t)buffer[1] << 32) | (buffer[2] << 24) | (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
            return value;
        }
    }

    uint64_t Utils::getValueUint64Endian(const uint8_t *buffer, uint8_t endian)
    {
        if (endian == Endian_Little)
        {
            uint64_t value = ((uint64_t)buffer[7] << 56) | ((uint64_t)buffer[6] << 48) | ((uint64_t)buffer[5] << 40) | ((uint64_t)buffer[4] << 32) | (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
            return value;
        }
        else
        {
            uint64_t value = ((uint64_t)buffer[0] << 56) | ((uint64_t)buffer[1] << 48) | ((uint64_t)buffer[2] << 40) | ((uint64_t)buffer[3] << 32) | (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
            return value;
        }
    }

    void Utils::setValueUint16Endian(uint8_t *buffer, const uint16_t value, uint8_t endian)
    {
        if (endian == Endian_Little)
        {
            buffer[0] = value & 0xff;
            buffer[1] = value >> 8;
        }
        else
        {
            buffer[1] = value & 0xff;
            buffer[0] = value >> 8;
        }
    }

    void Utils::setValueUint32Endian(uint8_t *buffer, const uint32_t value, uint8_t endian)
    {
        volatile uint8_t *p = buffer;
        if (endian == Endian_Little)
        {
            p[0] = value & 0xFF;
            p[1] = (value >> 8) & 0xFF;
            p[2] = (value >> 16) & 0xFF;
            p[3] = (value >> 24) & 0xFF;
        }
        else
        {
            p[3] = value & 0xFF;
            p[2] = (value >> 8) & 0xFF;
            p[1] = (value >> 16) & 0xFF;
            p[0] = (value >> 24) & 0xFF;
        }
    }

    std::string Utils::getCurrentProgramDir()
    {
        char szPath[512] = {0};
        std::string path = "";

#ifdef WIN32

        GetModuleFileName(NULL, szPath, sizeof(szPath) - 1);
        // printf("path:%s\n", szPath);
        path = szPath;
        path.erase(path.find_last_of('\\') + 1);

#else
        // path =  argv[0];

        int ret = readlink("/proc/self/exe", szPath, sizeof(szPath) - 1);
        // printf("ret:%d\n", ret);
        // printf("path:%s\n", szPath);

        path = szPath;
        path.erase(path.find_last_of('/') + 1);

#endif

        return path;
    }

    std::string Utils::getHostIp()
    {
        std::string hostipstr = "";

        boost::asio::io_service io_service;
        tcp::resolver resolver(io_service);
        tcp::resolver::query query(boost::asio::ip::host_name(), "");
        tcp::resolver::iterator iter = resolver.resolve(query);
        tcp::resolver::iterator end; // End marker.
        while (iter != end)
        {
            tcp::endpoint ep = *iter++;
            std::string ipstr = ep.address().to_string();

            if (Utils::ipIsValid(ipstr))
            {
                if (ipstr.find("127.") != 0)
                {
                    std::cout << ipstr << std::endl;
                    hostipstr = ipstr;
                }
            }
        }

        if (hostipstr == "")
            hostipstr = "0.0.0.0";

        return hostipstr;
    }

    void Utils::quaternionMultiply(const float *q1, const float *q2, float *result)
    {
        result[0] = q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2] - q1[3] * q2[3];
        result[1] = q1[0] * q2[1] + q1[1] * q2[0] + q1[2] * q2[3] - q1[3] * q2[2];
        result[2] = q1[0] * q2[2] - q1[1] * q2[3] + q1[2] * q2[0] + q1[3] * q2[1];
        result[3] = q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1] + q1[3] * q2[0];
    }
}

// end namespace XinTan
