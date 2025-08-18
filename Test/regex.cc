#include <iostream>
#include <string>
#include <regex>

int main()
{
    std::string str = "/hello/1234";
    // 匹配以 /numbers/起始，后边跟了一个或多个数字字符的字符串，并且在匹配的过程中提取这个匹配到的数字字符串
    std::regex e("/hello/(\\d+)");
    std::smatch matches;
    if (!std::regex_match(str, matches, e))
    {
        return -1;
    }

    for (auto &s : matches)
    {
        std::cout << s << std::endl;
    }

    return 0;
}
