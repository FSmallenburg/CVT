#pragma once

#include <cstdarg>
#include <cstdio>
#include <iostream>

/// Thin logging wrappers for the CVT application.
///
/// Use the stream-returning functions for C++-style output:
///   cvt::log::info() << "loaded " << n << " particles\n";
///
/// Use the printf-style functions when a format string is more convenient:
///   cvt::log::infof("frame %zu / %zu\n", current, total);
///
/// All functions are header-only inlines so there is no link-time cost.
namespace cvt::log
{

/// Returns a stream that writes to stdout with an "[info] " prefix.
/// Caller is responsible for appending '\n'.
inline std::ostream &info()
{
    return std::cout;
}

/// Returns a stream that writes to stderr. Intended for non-fatal warnings.
inline std::ostream &warn()
{
    return std::cerr;
}

/// Returns a stream that writes to stderr. Intended for error messages.
inline std::ostream &error()
{
    return std::cerr;
}

/// printf-style logging to stdout. Flushes after each call.
inline void infof(const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    std::vfprintf(stdout, format, argList);
    va_end(argList);
    std::fflush(stdout);
}

/// printf-style logging to stderr. Intended for non-fatal warnings.
inline void warnf(const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    std::vfprintf(stderr, format, argList);
    va_end(argList);
    std::fflush(stderr);
}

/// printf-style logging to stderr. Intended for error messages.
inline void errorf(const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    std::vfprintf(stderr, format, argList);
    va_end(argList);
    std::fflush(stderr);
}

/// va_list variant of errorf(), used when the caller already holds a va_list
/// (e.g. inside another variadic function).
inline void verrorf(const char *format, va_list argList)
{
    std::vfprintf(stderr, format, argList);
    std::fflush(stderr);
}

} // namespace cvt::log
