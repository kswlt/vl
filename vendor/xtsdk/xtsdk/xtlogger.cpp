/**************************************************************************************
* Copyright (C) 2022 Xintan Technology Corporation
*
* Author: Marco
***************************************************************************************/
#include "xtlogger.h"
#include <chrono>
#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <stdint.h>
#include <cstdio>

#ifdef WIN32
    #include <direct.h>
    #include <io.h>
#else
    #include <boost/filesystem.hpp>
#endif

namespace XinTan {


/******************************* log code  *************************************/
static std::ofstream info_log_file;
static std::ofstream warn_log_file;
static std::ofstream error_log_file;

static std::string g_filepath="";


#ifdef WIN32
int createDir(const std::string & filepath)
{
    int len = filepath.length();
    char tmpDirPath[256] = {0};
    for(int i=0; i < len; i++)
    {
        tmpDirPath[i] = filepath[i];
        if(tmpDirPath[i] == '\\' || tmpDirPath[i] == '/')
        {
            if(_access(tmpDirPath, 0) == -1)
            {
                int ret = _mkdir(tmpDirPath);
                if(ret == -1)
                    return ret;
            }
        }

    }

    return 0;
}
#else
int createDir(const std::string & filepath)
{	
    int len = filepath.length();
    char tmpDirPath[256] = {0};
    for(int i=0; i < len; i++)
    {
        tmpDirPath[i] = filepath[i];
        if(tmpDirPath[i] == '\\' || tmpDirPath[i] == '/')
        {
			if (!boost::filesystem::is_directory(tmpDirPath))
			if (!boost::filesystem::create_directory(tmpDirPath))
			{
				std::cout << "create_directories failed: " << tmpDirPath << std::endl;
				return -1;
			}
        }

    }	

    return 0;
}
#endif

void init_logger(const std::string & filepath)
{
    if(g_filepath == "")
    {
        createDir(filepath);

        bool isopened = false;
        if(info_log_file.is_open())
            isopened = true;
        if(warn_log_file.is_open())
            isopened = true;
        if(error_log_file.is_open())
            isopened = true;

        if(info_log_file.is_open() == false)
            info_log_file.open((filepath +"xtsdk_info.log").c_str(), std::ios::app);
        if(warn_log_file.is_open() == false)
            warn_log_file.open((filepath +"xtsdk_warn.log").c_str(), std::ios::app);
        if(error_log_file.is_open() == false)
            error_log_file.open((filepath +"xtsdk_err.log").c_str(), std::ios::app);

        info_log_file << "---------init_logger---------"<< std::endl;
        if(isopened == false)
        {
            //exception_reg();
            g_filepath = filepath;
        }
    }
}


std::ostream & get_stream(log_rank_t long_rank)
{
    return (INFO == long_rank) ? (info_log_file.is_open() ? info_log_file : std::cout)
            : ((WARRING == long_rank) ? (warn_log_file.is_open() ? warn_log_file : std::cerr)
                : (error_log_file.is_open() ? error_log_file : std::cerr));
}

void backupfile(log_rank_t log_rank)
{
    std::cout << "backupfile" << std::endl;

    if(log_rank == INFO)
        info_log_file.close();
    else if(log_rank == WARRING)
        warn_log_file.close();
    else
        error_log_file.close();

    std::string filename = (log_rank == INFO) ? "xtsdk_info.log" : ((log_rank == WARRING) ? "xtsdk_warn.log" : "xtsdk_err.log");

    std::string bkname = g_filepath+filename+".bk";

    std::remove(bkname.c_str());

    std::rename((g_filepath+filename).c_str(), bkname.c_str());

    if(log_rank == INFO)
        info_log_file.open((g_filepath+filename).c_str(), std::ios::app);
    else if(log_rank == WARRING)
        warn_log_file.open((g_filepath+filename).c_str(), std::ios::app);
    else
        error_log_file.open((g_filepath+filename).c_str(), std::ios::app);

}

void xtapplogger(const std::string & function, const std::string & logstr)
{
    auto now = std::chrono::system_clock::now();
    time_t  t = std::chrono::system_clock::to_time_t(now);

    auto localTm = localtime(&t);
    char time_str[64]={0};
    strftime(time_str, 64, "%Y%m%d-%H-%M-%S", localTm);

    std::ostream & outstream = get_stream(INFO);
    get_stream(INFO) << "[" << time_str << " " << "INFO" <<"] " << "App " << function << ": "  << logstr << std::endl;
    std::streampos ipos = outstream.tellp();

    if(ipos>10000000)
        backupfile(INFO);
}

void xtdlllogger(const char* pfunction, const char * plogstr)
{
    std::string function = pfunction;
    std::string logstr = plogstr;
    auto now = std::chrono::system_clock::now();
    time_t  t = std::chrono::system_clock::to_time_t(now);

    auto localTm = localtime(&t);
    char time_str[64] = { 0 };
    strftime(time_str, 64, "%Y%m%d-%H-%M-%S", localTm);

    std::ostream& outstream = get_stream(INFO);
    get_stream(INFO) << "[" << time_str << " " << "INFO" << "] " << "App " << function << ": " << logstr << std::endl;
    std::streampos ipos = outstream.tellp();

    if (ipos > 10000000)
        backupfile(INFO);
}

void xtloggerErr( const std::string & logstr)
{
    auto now = std::chrono::system_clock::now();
    time_t  t = std::chrono::system_clock::to_time_t(now);

    auto localTm = localtime(&t);
    char time_str[64]={0};
    strftime(time_str, 64, "%Y%m%d-%H-%M-%S", localTm);

    std::ostream & outstream = get_stream(ERR);
    get_stream(ERR) << "[" << time_str << " " << "ERR" <<"] "  << logstr << std::endl;
    std::streampos ipos = outstream.tellp();

    if(ipos>10000000)
        backupfile(ERR);
}


void xtcodelogger(std::string & logtag, log_rank_t log_rank, const std::string & function, const std::string & logstr)
{
    auto now = std::chrono::system_clock::now();
    time_t  t = std::chrono::system_clock::to_time_t(now);

    auto localTm = localtime(&t);
    char time_str[64]={0};
    strftime(time_str, 64, "%Y%m%d-%H-%M-%S", localTm);

    std::string log_rank_str = (log_rank == INFO) ? ("INFO") : ((log_rank == WARRING) ? "WARRING" : "ERROR");

    if(logtag == "")
        get_stream(log_rank) << "[" << time_str << " " << log_rank_str <<"] " << function << ": " << logstr << std::endl;
    else
        get_stream(log_rank) << "[" << time_str << " " << log_rank_str <<"] [" << logtag+"] " << function << ": " << logstr << std::endl;

    std::ostream & outstream = get_stream(log_rank);
    std::streampos ipos = outstream.tellp();

    if(ipos>10000000)
        backupfile(log_rank);
}

/************************************** exception code  *************************************/
#ifdef WIN32

#include <windows.h>
#include <Dbghelp.h>
using namespace std;

#pragma auto_inline (off)
#pragma comment(lib, "DbgHelp")

LONG WINAPI UnhandledExcepFilter(PEXCEPTION_POINTERS pExcepPointers){
    HANDLE lhDumpFile = CreateFile((char *)"./xtlog/mini.dmp", GENERIC_WRITE, 0, NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL ,NULL);

   // std::string excpstr = pExcepPointers->ExceptionRecord->ExceptionInformation;

    xtloggerErr("------------crash exception------------");

    MINIDUMP_EXCEPTION_INFORMATION loExceptionInfo;

    loExceptionInfo.ExceptionPointers = pExcepPointers;

    loExceptionInfo.ThreadId = GetCurrentThreadId();

    loExceptionInfo.ClientPointers = TRUE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),lhDumpFile, MiniDumpNormal, &loExceptionInfo, NULL, NULL);

    CloseHandle(lhDumpFile);

    return EXCEPTION_EXECUTE_HANDLER;
}

void exception_reg()
{
    // 注册异常处理函数
    //LPTOP_LEVEL_EXCEPTION_FILTER Top =
    SetUnhandledExceptionFilter(UnhandledExcepFilter);

//    char * xxx = nullptr;
//    *xxx = '1';
}


#else

#include <signal.h>
#include <execinfo.h>
void backtrace_handler(int sig)
{
    int j, nptrs;
    void * buffer[256];
    char strbuffer[1024];
    char ** strings;

	if((sig == 11)||(sig == 2))
		goto exit;
		
    xtloggerErr("-----------crash---------------"+std::to_string(sig));

    nptrs = backtrace(buffer, 256);
    strings = backtrace_symbols(buffer, nptrs);
    if( strings == nullptr)
        exit(0);

    if(nptrs <= 2)
        goto exit;

    for(j = 2; j < nptrs; j++)
    {
        memset(strbuffer,0,sizeof(strbuffer));
        sprintf(strbuffer, "[%02d] %s \n", j-2, strings[j]);
        xtloggerErr(strbuffer);
		printf("%s", strbuffer);
    }

exit:
    free(strings);
    strings = nullptr;
    exit(0);

}

int32_t backtrace_reg(struct sigaction * sig_act)
{
    int32_t ret = 0;
    //int i, sigs[] = {SIGILL, SIGBUS, SIGFPE, SIGSEGV, SIGINT,SIGABRT};
	int i, sigs[] = {SIGILL, SIGBUS, SIGFPE, SIGSEGV, SIGINT, SIGABRT};
    sig_act->sa_handler = backtrace_handler;

    for(i=0; i < sizeof(sigs)/sizeof(sigs[0]); i++)
    {
        ret = sigaction(sigs[i],sig_act, 0);
        if(ret != 0)
            return -1;
    }
	return 0;
}


static struct sigaction sig;
void exception_reg()
{
    backtrace_reg(&sig);
}
#endif

} //end namespace XinTan

