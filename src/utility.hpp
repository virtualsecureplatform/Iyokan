#ifndef VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP

#include <cassert>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

#include <fmt/printf.h>

namespace utility {
namespace impl {
template <class Arg>
std::ostream& fokImpl(std::ostream& os, Arg&& arg)
{
    os << arg;
    return os;
}

inline std::ostream& fokImpl(std::ostream& os,
                             const std::chrono::system_clock::time_point& tp)
{
    using namespace std::chrono;

    std::time_t tt = system_clock::to_time_t(tp);
    std::tm* t = std::localtime(&tt);
    auto fraction = duration_cast<milliseconds>(tp.time_since_epoch()) -
                    duration_cast<milliseconds>(
                        duration_cast<seconds>(tp.time_since_epoch()));

    fmt::fprintf(os, "%04d-%02d-%02d %02d:%02d:%02d.%03ld", t->tm_year + 1900,
                 t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
                 fraction.count());

    return os;
}
}  // namespace impl

template <class... Args>
std::string fok(Args&&... args)
{
    std::stringstream ss;
    (impl::fokImpl(ss, std::forward<Args>(args)), ...);
    return ss.str();
}

inline std::vector<std::string> regexMatch(const std::string& text,
                                           const std::regex& re)
{
    std::vector<std::string> ret;
    std::smatch m;
    if (!std::regex_match(text, m, re))
        return ret;
    for (auto&& elm : m)
        ret.push_back(elm.str());
    return ret;
}

inline bool isPowerOfTwo(uint64_t n)
{
    return (n != 0) && ((n & (n - 1)) == 0);
}

inline uint64_t log2(uint64_t n)
{
    return std::log2(n);
}

inline std::ostream& operator<<(std::ostream& os,
                                const std::chrono::system_clock::time_point& tp)
{
    impl::fokImpl(os, tp);
    return os;
}

inline int ctz(unsigned int n)
{
    return __builtin_ctz(n);
}

inline int ctz(unsigned long n)
{
    return __builtin_ctzl(n);
}

inline int ctz(unsigned long long n)
{
    return __builtin_ctzll(n);
}

inline std::unique_ptr<std::ofstream> openOfstream(const std::string& filepath)
{
    auto ofs = std::make_unique<std::ofstream>(filepath, std::ios::binary);
    if (!*ofs)
        error::die("Can't open the file to write in: ", filepath);
    return ofs;
}

}  // namespace utility

#endif
