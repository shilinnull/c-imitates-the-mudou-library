#include "http.hpp"
#include <filesystem>
#include <fstream>

int main()
{
    std::string s;
    
    Util::ReadFile("makefile", &s);
    std::ofstream file("makefile1");
    file << s;

    // std::string s = "hello,www,aaa,,";
    // std::vector<std::string> res;
    // Util::Split(s, ",", &res);
    // for(auto s : res)
    //     std::cout << s << std::endl;
    return 0;
}