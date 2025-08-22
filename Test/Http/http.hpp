#ifndef __HTTP_HPP__
#define __HTTP_HPP__

#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include "../../source/server.hpp"

class Util
{
public:
    static ssize_t Split(const std::string &src, const std::string &sep, std::vector<std::string> *res)
    {
        for (size_t index = 0; index < src.size();)
        {
            ssize_t pos = src.find(sep, index); // 从index开始查找
            if (pos == std::string::npos)
            {
                res->push_back(src.substr(index));
                break;
            }
            if (pos == index) // 跳过sep
            {
                index += sep.size();
                continue;
            }
            res->push_back(src.substr(index, pos - index));
            index = pos + sep.size();
        }
        return res->size();
    }

    static bool ReadFile(const std::string &filename, std::string *buf)
    {
        // 打开文件
        std::ifstream ifs(filename, std::ios::binary);
        if (ifs.is_open() == false)
        {
            LOG(LogLevel::WARNING) << "file open failed! filename: " << filename;
            return false;
        }
        size_t fsize = 0;
        ifs.seekg(0, std::ios::end); // 跳转读写位置到末尾
        fsize = ifs.tellg();         // 获取当前读写位置相对于起始位置的偏移量，从末尾偏移刚好就是文件大小
        ifs.seekg(0, std::ios::beg); // 跳转到起始位置
        buf->resize(fsize);          // 开辟文件大小的空间
        ifs.read(&(*buf)[0], fsize); // 读取数据
        if (ifs.good() == false)
        {
            LOG(LogLevel::WARNING) << "read file content failed! filename: " << filename;
            ifs.close();
            return false;
        }
        ifs.close();
        return true;
    }
};

#endif