#ifndef __HTTP_HPP__
#define __HTTP_HPP__

#include <iostream>
#include <string>
#include <vector>
// #include "../../source/server.hpp"

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
};

#endif