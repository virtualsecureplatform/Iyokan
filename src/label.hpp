#ifndef VIRTUALSECUREPLATFORM_CONFIG_NAME_HPP
#define VIRTUALSECUREPLATFORM_CONFIG_NAME_HPP

#include <optional>
#include <string>

struct ConfigName {
    std::string nodeName, portName;
    int portBit;

    // Although all member variables of ConfigName are public, we make
    // operator<< and operator< its friend function for clarity.
    friend std::ostream& operator<<(std::ostream& os, const ConfigName& c);
    friend bool operator<(const ConfigName& lhs, const ConfigName& rhs);
};

using UID = uint64_t;

struct Label {
    // String literals for member variable `kind`.
    // If label is for inputs or outputs, these member variable must be used,
    // that is, kind == Label::INPUT or kind == Label::OUTPUT.
    // The instances of these variables exist in iyokan_nt.cpp.
    // We CANNOT use (C++17) `inline` here, because `inline` does NOT guarantee
    // that these variables have the same value in different compilation units
    // (FIXME: This behaviour is confirmed only on g++-10. We need to check the
    // C++ standard).
    static const char* const INPUT;
    static const char* const OUTPUT;

    UID uid;
    const char* const kind;  // Stores a string literal
    std::optional<ConfigName> cname;

    Label(UID uid, const char* const kind, std::optional<ConfigName> cname)
        : uid(uid), kind(kind), cname(std::move(cname))
    {
    }
};

#endif
