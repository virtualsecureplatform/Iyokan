#include "error_nt.hpp"

#include <backward.hpp>

#include <cassert>

namespace nt::error {
void initialize()
{
#ifdef NDEBUG
    // Release
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
#else
    // Debug
    loguru::g_stderr_verbosity = loguru::Verbosity_1;  // Show LOG_DBG messages
#endif
}

void abortWithBacktrace()
{
    using namespace backward;

#ifndef NDEBUG
    {
        // Print backtrace
        LOG_F(ERROR, "Preparing backtrace...");
        StackTrace st;
        st.load_here(32);
        Printer p;
        p.print(st, stderr);
    }
#endif

    // Abort
    std::exit(EXIT_FAILURE);
}

}  // namespace nt::error

#include <loguru.cpp>
