#include "../../source/server.hpp"

class EchoServer
{
private:
    void OnConnected(const PtrConnection &conn)
    {
        LOG(LogLevel::DEBUG) << "NEW CONNECTION " << conn.get();
    }
    void OnClosed(const PtrConnection &conn)
    {
        LOG(LogLevel::DEBUG) << "CLOSE CONNECTION " << conn.get();
    }
    void OnMessage(const PtrConnection &conn, Buffer *buf)
    {
        conn->Send(buf->ReadPosition(), buf->ReadableSize());
        buf->MoveReadOffset(buf->ReadableSize());
        conn->Shutdown();
    }

public:
    EchoServer(int port)
        : _server(port)
    {
        _server.SetThreadCount(2);
        _server.EnableInactiveRelease(10);
        _server.SetClosedCallback(std::bind(&EchoServer::OnClosed, this, std::placeholders::_1));
        _server.SetConnectedCallback(std::bind(&EchoServer::OnConnected, this, std::placeholders::_1));
        _server.SetMessageCallback(std::bind(&EchoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
    void Start()
    {
        _server.Start();
    }

private:
    TcpServer _server;
};