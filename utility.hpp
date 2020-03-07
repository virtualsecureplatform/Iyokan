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

}  // namespace utility

#endif
