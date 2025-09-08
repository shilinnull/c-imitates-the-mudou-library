cmake_minimum_required(VERSION 3.18)
project(mymuduo)

# cmake => makefile   make
# mymuduo最终编译成so动态库，设置动态库的路径，放在根目录的lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置调试信息 以及 启动C++11语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

# 定义参与编译的源代码文件 
set(LIB_SRC_LIST Logger.cc Timestamp.cc)
# 编译生成动态库mymuduo
add_library(mymuduo SHARED ${LIB_SRC_LIST})

# 编译生成可执行文件
add_executable(mytest InetAddress.cc)

include(CTest)
add_test(
    NAME case_test
    COMMAND mytest
)