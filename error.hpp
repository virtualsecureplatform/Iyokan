#ifndef VIRTUALSECUREPLATFORM_IYOKAN_ERROR_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_ERROR_HPP

#include <iostream>

#include <backward.hpp>

namespace error {
template <class... Args>
[[noreturn]] void die(Args... args)
{
    using namespace backward;

    // Print error message
    std::cerr << "ERROR!" << std::endl;
    (std::cerr << ... << args);
    std::cerr << std::endl;

    // Print backtrace
    StackTrace st;
    st.load_here(32);
    Printer p;
    p.print(st, std::cerr);

    // Abort
    std::exit(EXIT_FAILURE);
}
}  // namespace error

#endif
