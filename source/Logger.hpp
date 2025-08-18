#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <iostream>
#include <string>
#include <ctime>
#include <unistd.h>
#include <memory>
#include <sstream>
#include <filesystem>
#include <fstream>

#include "LockGuard.hpp"

const std::string logdefaultdir = "./log";
const static std::string logfilename = "server.log";

// 日志等级
enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

std::string Level2String(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "Debug";
    case LogLevel::INFO:
        return "Info";
    case LogLevel::WARNING:
        return "Warning";
    case LogLevel::ERROR:
        return "Error";
    case LogLevel::FATAL:
        return "Fatal";
    default:
        return "Unknown";
    }
}

std::string getCurrentTime()
{
    // 获取当前时间戳
    time_t currtime = time(nullptr);

    // 转换时间
    struct tm t;
    localtime_r(&currtime, &t);

    char timebuffer[64];
    snprintf(timebuffer, sizeof(timebuffer), "%4d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
    return timebuffer;
}

class LogStrategy
{
public:
    virtual ~LogStrategy() = default;
    virtual void SyncLog(const std::string &logmessage) = 0; // 刷新日志
};

class ConsoleLogStrategy : public LogStrategy
{
public:
    ~ConsoleLogStrategy()
    {
    }
    void SyncLog(const std::string &logmessage) override
    {
        {
            LockGuard lockGuard(&_lock);
            std::cout << logmessage << std::endl;
        }
    }

private:
    Mutex _lock;
};

class FileLogStrategy : public LogStrategy
{
public:
    FileLogStrategy(const std::string &dir = logdefaultdir, const std::string &logfilename = logfilename)
        : _dir_path_name(dir), _file_name(logfilename)
    {
        {
            LockGuard lockGuard(&_lock);
            if (std::filesystem::exists(_dir_path_name))
            {
                return;
            }
            try
            {
                std::filesystem::create_directories(_dir_path_name);
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                std::cerr << e.what() << "\r\n";
            }
        }
    }
    void SyncLog(const std::string &logmessage) override
    {
        {
            LockGuard lockGuard(&_lock);
            std::string target = _dir_path_name;
            target += "/";
            target += _file_name;
            std::ofstream out(target.c_str(), std::ios::app);
            if (!out.is_open())
            {
                std::cerr << "Failed to open log file: " << target << "\n";
                return;
            }
            out << logmessage << "\n";
            out.close();
        }
    }
    ~FileLogStrategy() {}

private:
    std::string _dir_path_name;
    std::string _file_name;
    Mutex _lock;
};

class Logger
{
public:
    Logger()
    {
        EnableConsoleLogStrategy();
    }

    void EnableConsoleLogStrategy()
    {
        _strategy = std::make_unique<ConsoleLogStrategy>();
    }

    void EnableFileLogStrategy()
    {
        _strategy = std::make_unique<FileLogStrategy>();
    }

    // 一条消息
    class LogMessage
    {
    public:
        LogMessage(LogLevel level, std::string filename, int line, Logger &logger)
            : _curr_time(getCurrentTime()), _level(level), _pid(getpid()), _filename(filename), _line(line), _logger(logger)
        {
            std::stringstream ss;
            ss << "[" << _curr_time << "] "
               << "[" << Level2String(_level) << "] "
               << "[" << _pid << "] "
               << "[" << _filename << "] "
               << "[" << _line << "] "
               << ": ";
            _loginfo = ss.str();
        }

        template <typename T>
        LogMessage &operator<<(const T &info)
        {
            std::stringstream ss;
            ss << info;
            _loginfo += ss.str();
            return *this;
        }

        ~LogMessage()
        {
            // 在析构函数中刷新日志
            if (_logger._strategy)
            {
                _logger._strategy->SyncLog(_loginfo);
            }
        }

    private:
        std::string _curr_time; // 当前时间
        LogLevel _level;        // 告警级别
        pid_t _pid;             // 进程pid
        std::string _filename;  // 文件名字
        int _line;              // 行号

        std::string _loginfo; // 信息主体
        Logger &_logger;      // 提供刷新策略的具体做法
    };

    LogMessage operator()(LogLevel level, std::string filename, int line)
    {
        return LogMessage(level, filename, line, *this);
    }

    ~Logger() {}

private:
    std::unique_ptr<LogStrategy> _strategy;
};

Logger logger; // 全局日志对象

#define LOG(level) logger(level, __FILE__, __LINE__)
#define EnableConsoleLog() logger.EnableConsoleLogStrategy()
#define EnableFileLog() logger.EnableFileLogStrategy()

#endif // __LOGGER_HPP__