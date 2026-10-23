#pragma once

#include <cstddef>
#include <string>

namespace golding
{

/**
 * @brief 一张登记表的计数与最后一次驳回原因。
 *
 * 存在的理由：六张表原先只有 `count()`，驳回只打一行日志。于是"应当驳回 3 次"这件事
 * 在自检里**无法断言**——只能人肉看日志。装配阶段同样需要知道"我提交了 N 项、
 * 被吃掉了 M 项"，否则失败项计数只能靠 `RegisterResult` 的调用方各自累加，各写一份。
 *
 * 口径刻意只有四个数：**接受数、驳回数、最后一次驳回原因、表长**。不做按原因分类的
 * 直方图——那会把这张结构体变成一个小小的可观测性系统，而它只需要能支撑一句断言。
 *
 * `lastReject` 存人话（"同名已存在：home.core"），不是枚举名：日志和断言打印的是它，
 * 枚举名对读日志的人没有信息量。
 */
struct RegistryStats
{
    /**被接受的登记次数（含重复调用时被驳回的那些**不**计入）。*/
    std::size_t accepted = 0;
    /**被驳回的登记次数。*/
    std::size_t rejected = 0;
    /**最后一次驳回的人话原因；没驳回过是空串。*/
    std::string lastReject;

    void reset()
    {
        accepted = 0;
        rejected = 0;
        lastReject.clear();
    }

    void accept() { ++accepted; }

    void reject(const std::string& reason)
    {
        ++rejected;
        lastReject = reason;
    }
};

} // namespace golding
