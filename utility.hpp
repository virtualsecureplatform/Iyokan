#ifndef VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP

#include <cassert>
#include <cmath>
#include <iostream>
#include <regex>
#include <vector>

namespace utility {
template <class... Args>
std::string fok(Args... args)
{
    std::stringstream ss;
    (ss << ... << args);
    return ss.str();
}

inline std::vector<std::string> regexMatch(const std::string &text,
                                           const std::regex &re)
{
    std::vector<std::string> ret;
    std::smatch m;
    if (!std::regex_match(text, m, re))
        return ret;
    for (auto &&elm : m)
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

}  // namespace utility

#endif
