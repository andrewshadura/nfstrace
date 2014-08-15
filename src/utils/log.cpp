//------------------------------------------------------------------------------
// Author: Pavel Karneliuk
// Description: Reentrant logger implementation
// Copyright (c) 2013 EPAM Systems
//------------------------------------------------------------------------------
/*
    This file is part of Nfstrace.

    Nfstrace is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2 of the License.

    Nfstrace is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Nfstrace.  If not, see <http://www.gnu.org/licenses/>.
*/
//------------------------------------------------------------------------------
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <stdexcept>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "out.h"

#ifndef LOG_PATH
#define LOG_PATH "/tmp/nfstrace"
#endif

#include "utils/log.h"
//------------------------------------------------------------------------------
/*  http://www.unix.org/whitepapers/reentrant.html
    The POSIX.1 and C-language functions that operate on character streams
    (represented by pointers to objects of type FILE) are required by POSIX.1c
    to be implemented in such a way that reentrancy is achieved
    (see ISO/IEC 9945:1-1996, §8.2). This requirement has a drawback; it imposes
    substantial performance penalties because of the synchronization that must
    be built into the implementations of the functions for the sake of reentrancy.
*/
//------------------------------------------------------------------------------
namespace NST
{
namespace utils
{

static FILE* log_file = nullptr;
static bool  own_file = false;

std::string get_pid()
{
    char buff[8]={"\0"};
    sprintf(buff,"%d",getpid());
    return std::string(buff);
}

Log::Global::Global(const std::string& path)
{
    if(log_file != nullptr)
    {
        throw std::runtime_error{"Global Logger already have been created"};
    }

    // default is stderr
    if(path.empty())
    {
        log_file = ::stderr;
        return;
    }

    struct stat s;
    if (stat(LOG_PATH, &s))//check destination folder exists
    {
        if(mkdir(LOG_PATH,ALLPERMS))//create directory fot nfs logs
        {
            throw std::runtime_error{"Logger can not create log directory: " + std::string(LOG_PATH)};
        }
        if(utils::Out message{})
        {
            message << "Add log folder: " << std::string(LOG_PATH);
        }
    }

    std::string tmp(std::string(LOG_PATH) + "/" + path + ".log");
    FILE* file = fopen(tmp.c_str(), "w");
    if(file == NULL)
    {
        throw std::runtime_error{"Logger can not open file for write: " + path};
    }

    chmod(tmp.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH); //0666

    if(flock(fileno(file), LOCK_EX | LOCK_NB))
    {
        fclose(file);
        tmp = std::string(LOG_PATH) + "/" + path + "-" + get_pid() + ".log";
        file = fopen(tmp.c_str(), "w");
        if(file == NULL)
        {
            throw std::runtime_error{"Logger can not open file for write: " + path};
        }
        chmod(tmp.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH); //0666
        if(flock(fileno(file), LOCK_EX | LOCK_NB))
        {
            fclose(file);
            throw std::runtime_error{"Can't lock log file: " + tmp};
        }
    }

    log_file = file;
    own_file = true;
}
Log::Global::~Global()
{
    if(own_file)
    {
        flock(fileno(log_file), LOCK_UN);
        fclose(log_file);
    }
}

Log::Log()
: std::stringbuf {ios_base::out}
, std::ostream   {nullptr}
{
    std::stringbuf::setp(buffer, buffer+sizeof(buffer));
    std::ostream::init(static_cast<std::stringbuf*>(this));
    std::ostream::put('\n');
}
Log::~Log()
{
    size_t len = pptr() - pbase();
    fwrite(pbase(), len, 1, log_file);
}

void Log::message(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
}

void Log::flush()
{
    fflush(log_file);
}

} // namespace utils
} // namespace NST
//------------------------------------------------------------------------------
