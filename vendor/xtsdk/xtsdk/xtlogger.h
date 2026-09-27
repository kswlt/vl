/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#pragma once

#include <string>

namespace XinTan {
 //日志文件类型
typedef enum log_rank
{
    INFO,
    WARRING,
    ERR
} log_rank_t;

//初始化日志文件
void init_logger(const std::string & filepath);

void xtcodelogger(std::string & logtag, log_rank_t log_rank, const std::string & function, const std::string & logstr);


void exception_reg();

#define XTLOGINFO(logstr) xtcodelogger(logtagname, INFO, __FUNCTION__, logstr)
#define XTLOGWRN(logstr) xtcodelogger(logtagname, WARRING, __FUNCTION__, logstr)
#define XTLOGERR(logstr) xtcodelogger(logtagname, ERR, __FUNCTION__, logstr)


#define XTLOGINFOEXT(logtag, logstr) xtcodelogger(logtag, INFO, __FUNCTION__, logstr)
#define XTLOGWRNEXT(logtag, logstr) xtcodelogger(logtag, WARRING, __FUNCTION__, logstr)
#define XTLOGERREXT(logtag, logstr) xtcodelogger(logtag, ERR, __FUNCTION__, logstr)


} //end namespace xintan

