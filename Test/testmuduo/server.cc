#include <iostream>
#include <functional>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>

using namespace std;
using namespace muduo;
using namespace muduo::net;

class ChatServer
{
public:
    ChatServer(EventLoop* loop,  // 事件循环
            const InetAddress& listenAddr, // IP+Port
            const string& nameArg)
        : _server(loop, listenAddr, nameArg), _loop(loop)
    {
        // 注册创建断开回调
        _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, placeholders::_1));
        _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, placeholders::_1, placeholders::_2, placeholders::_3));
        
        _server.setThreadNum(4);
    }

    void Start() {
        _server.start();
    }

    ~ChatServer() {}
private:
    void onConnection(const TcpConnectionPtr& conn) {
        if(conn->connected())
        {
            cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << endl;
        }
        else
        {
            cout << conn->peerAddress().toIpPort() << " -> " << conn->localAddress().toIpPort() << endl;
            conn->shutdown();
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer *buffer, Timestamp time) {
        string buf = buffer->retrieveAllAsString();
        cout << "recv data: " << buf << " time: " << time.toString() << endl;
        conn->send(buf);
    }

    TcpServer _server;
    EventLoop *_loop;
};

int main()
{
    EventLoop loop;
    InetAddress addr("127.0.0.1", 8085);
    ChatServer server(&loop, addr, "ChatServer");
    server.Start();
    loop.loop();
    return 0;
}
