#include <iostream>
#include <string>
#include <vector>
#include <functional>

using namespace std;

void Print(const std::string &str, int num)
{
    std::cout << str << " " << num << std::endl;
}

int main()
{
    // auto fun = std::bind(Print, "hello", std::placeholders::_1);
    // fun(1);
    using Task = std::function<void()>;
    std::vector<Task> arry;
    arry.push_back(std::bind(Print, "hello", 1));
    arry.push_back(std::bind(Print, "haha", 2));
    arry.push_back(std::bind(Print, "world", 3));

    for(auto &f : arry)
    {
        f();
    }
    return 0;
}