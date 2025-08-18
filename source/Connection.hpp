#ifndef __CONNECTION_HPP__
#define __CONNECTION_HPP__

#include <string>
#include <cstring>
#include <vector>
#include <cassert>
#include <cstdint>

#include "Logger.hpp"

#define BUFFER_DEFAULT_SIZE 1024 // 默认缓冲区大小

class Buffer
{
public:
    Buffer() : _buffer(BUFFER_DEFAULT_SIZE), _reader_idx(0), _writer_idx(0) {}
    ~Buffer() {}

    // 获取缓冲区起始地址
    char *Begin() { return &*_buffer.begin(); }

    // 获取当前写入起始地址
    char *WritePosition() { return Begin() + _writer_idx; }

    // 获取当前读取起始地址
    char *ReadPosition() { return Begin() + _reader_idx; }

    // 获取前沿空闲空间大小--写偏移之后的空闲空间 写偏移之后的空闲空间, 总体空间大小减去写偏移
    uint64_t FrontFreeSize() { return _reader_idx; }

    // 获取后沿空闲空间大小--读偏移之前的空闲空间
    uint64_t BackFreeSize() { return _buffer.size() - _writer_idx; }

    // 获取可读数据大小       写偏移 - 读偏移
    uint64_t ReadableSize() { return _writer_idx - _reader_idx; }

    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
            return;
        // 向后移动的大小，必须小于可读数据大小
        assert(len <= ReadableSize());
        _reader_idx += len;
    }

    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        // 向后移动的大小，必须小于当前后边的空闲空间大小
        assert(len <= BackFreeSize());
        _writer_idx += len;
    }

    // 确保可写空间足够（整体空闲空间够了就移动数据，否则就扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        // 如果末尾空闲空间大小足够，直接返回
        if (BackFreeSize() >= len)
            return;
        // 末尾空闲空间不够，则判断：加上起始位置的空闲空间大小是否足够, 够了就将数据移动到起始位置
        if (FrontFreeSize() + BackFreeSize() >= len)
        {
            // 将数据移动到起始位置
            size_t rsz = ReadableSize();
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin());
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            // 总体空间不够，则需要扩容，不移动数据，直接给写偏移之后扩容足够空间即可
            _buffer.resize(_writer_idx + len);
            LOG(LogLevel::DEBUG) << "RESIZE " << _writer_idx + len;
        }
    }

    // 写入数据
    void Write(const void *data, uint64_t len)
    {
        // 1. 保证有足够空间，2. 拷贝数据进去
        EnsureWriteSpace(len);
        std::copy(static_cast<const char *>(data), static_cast<const char *>(data) + len, WritePosition());
    }

    // 写入数据并压入
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }

    // 写入字符串
    void WriteString(const std::string &s)
    {
        Write(s.c_str(), s.size());
    }

    // 写入字符串并压入
    void WriteStringAndPush(const std::string &s)
    {
        WriteString(s);
        MoveWriteOffset(s.size());
    }

    // 写入缓冲区
    void WriteBuffer(Buffer &buf)
    {
        Write(buf.ReadPosition(), buf.ReadableSize());
    }

    // 写入缓冲区并压入
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadableSize());
    }

    // 读取数据
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadableSize());
        std::copy(ReadPosition(), ReadPosition() + len, static_cast<char *>(buf));
    }

    // 读取数据并弹出
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    // 读取字符串
    std::string ReadAsString(uint64_t len)
    {
        // 要求要获取的数据大小必须小于可读数据大小
        assert(len <= ReadableSize());
        std::string s(ReadPosition(), len);
        return s;
    }

    // 读取字符串并弹出
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string s = ReadAsString(len);
        MoveReadOffset(len);
        return s;
    }

    // 找到CRLF
    char *FindCRLF()
    {
        char *p = (char *)memchr(ReadPosition(), '\n', ReadableSize());
        return p;
    }

    // 读取一行
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == nullptr)
            return "";
        return ReadAsString(pos - ReadPosition() + 1); // 包含了\n
    }

    // 读取一行并弹出
    std::string GetLineAndPop()
    {
        std::string s = GetLine();
        MoveReadOffset(s.size());
        return s;
    }

    // 清空缓冲区
    void Clear()
    {
        _reader_idx = 0;
        _writer_idx = 0;
    }

private:
    std::vector<char> _buffer; // vector 缓冲区
    uint64_t _reader_idx;      // 读偏移
    uint64_t _writer_idx;      // 写偏移
};

#endif