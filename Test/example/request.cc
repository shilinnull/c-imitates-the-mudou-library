#include <iostream>
#include <string>
#include <regex>

int main()
{
    std::string s = "GET /shilinnull/login?user=xiaoming&pass=123123 HTTP/1.1\r\n";

    std::smatch matches;
    std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?");
    // GET|HEAD|POST|PUT|DELETE   表示匹配并提取其中任意一个字符串
    // [^?]*     [^?]匹配非问号字符， 后边的*表示0次或多次
    // \\?(.*)   \\?  表示原始的?字符 (.*)表示提取?之后的任意字符0次或多次，直到遇到空格
    // HTTP/1\\.[01]  表示匹配以HTTP/1.开始，后边有个0或1的字符串
    // (?:\n|\r\n)?   (?: ...)表示匹配某个格式字符串，但是不提取， 最后的？表示的是匹配前边的表达式0次或1次
    if (!std::regex_match(s, matches, e))
        return 1;
    
    for (auto &m : matches)
        std::cout << m << std::endl;
    return 0;
}