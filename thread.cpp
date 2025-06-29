#include "thread.h"
#include <stdio.h>

/**
 * @brief 构造函数，初始化线程基类
 *
 * 基类构造函数不需要特殊初始化，成员变量在类定义中有初始值
 */
Thread::Thread()
{
    // abort_ = 0; // 在类定义中已初始化为0
    // thread_ = nullptr; // 在类定义中已初始化为nullptr
}

/**
 * @brief 析构函数，释放线程资源
 *
 * 基类析构函数为虚函数，确保派生类对象正确析构
 */
Thread::~Thread()
{
    // 派生类应在析构前调用Stop()确保线程停止
}

/**
 * @brief 启动线程
 * @return 成功返回0
 *
 * 此为基类的默认实现，派生类应重写此方法
 */
int Thread::Start()
{
    return 0;
}

/**
 * @brief 停止线程
 * @return 成功返回0
 *
 * 设置终止标志并等待线程结束，释放线程资源
 */
int Thread::Stop()
{
    printf("%s(%d)\n", __FUNCTION__, __LINE__);
    // 设置终止标志，通知线程退出
    abort_ = 1;
    
    // 如果线程存在，等待线程结束并释放资源
    if(thread_) {
        thread_->join();  // 等待线程结束
        delete thread_;   // 释放线程对象
        thread_ = nullptr;
    }
    return 0;
}
