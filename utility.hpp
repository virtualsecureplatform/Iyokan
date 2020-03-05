#ifndef VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_UTILITY_HPP

#include <cassert>
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

inline uint64_t u8vec2i(const std::vector<uint8_t> &src, int start = 0,
                        int end = -1)
{
    if (end == -1)
        end = src.size();
    assert(end - start < 64);
    uint64_t ret = 0;
    for (size_t i = start; i < end; i++)
        ret |= (src[i] << i);
    return ret;
}

}  // namespace utility

#endif
