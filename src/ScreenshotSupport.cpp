#include "ScreenshotSupport.h"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/file.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace
{

std::tm localTime(std::time_t timeValue)
{
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &timeValue);
#else
    localtime_r(&timeValue, &result);
#endif
    return result;
}

} // namespace

void ScreenshotCallback::fatal(const char *filePath, uint16_t line, bgfx::Fatal::Enum code,
                               const char *message)
{
    std::fprintf(stderr, "bgfx fatal %u at %s:%u: %s\n", static_cast<unsigned>(code),
                 filePath != nullptr ? filePath : "<unknown>", static_cast<unsigned>(line),
                 message != nullptr ? message : "<no message>");
    std::fflush(stderr);
    if (code != bgfx::Fatal::DebugCheck)
    {
        std::abort();
    }
}

void ScreenshotCallback::traceVargs(const char *filePath, uint16_t line, const char *format,
                                    va_list argList)
{
    std::fprintf(stderr, "bgfx trace %s:%u: ", filePath != nullptr ? filePath : "<unknown>",
                 static_cast<unsigned>(line));
    std::vfprintf(stderr, format, argList);
    std::fflush(stderr);
}

void ScreenshotCallback::profilerBegin(const char *, uint32_t, const char *, uint16_t)
{
}

void ScreenshotCallback::profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t)
{
}

void ScreenshotCallback::profilerEnd()
{
}

uint32_t ScreenshotCallback::cacheReadSize(uint64_t)
{
    return 0;
}

bool ScreenshotCallback::cacheRead(uint64_t, void *, uint32_t)
{
    return false;
}

void ScreenshotCallback::cacheWrite(uint64_t, const void *, uint32_t)
{
}

void ScreenshotCallback::screenShot(const char *filePath, uint32_t width, uint32_t height,
                                    uint32_t pitch, bgfx::TextureFormat::Enum format,
                                    const void *data, uint32_t, bool yflip)
{
    if (!writeScreenshot(filePath, width, height, pitch, format, data, yflip))
    {
        std::fprintf(stderr, "Failed to write screenshot: %s\n",
                     filePath != nullptr ? filePath : "<unknown>");
        std::fflush(stderr);
        return;
    }

    std::printf("Saved snapshot: %s\n", filePath != nullptr ? filePath : "<unknown>");
    std::fflush(stdout);
}

void ScreenshotCallback::captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum,
                                      bool)
{
}

void ScreenshotCallback::captureEnd()
{
}

void ScreenshotCallback::captureFrame(const void *, uint32_t)
{
}

bool ScreenshotCallback::writeScreenshot(const char *filePath, uint32_t width, uint32_t height,
                                         uint32_t pitch, bgfx::TextureFormat::Enum format,
                                         const void *data, bool yflip)
{
    if (filePath == nullptr || data == nullptr || width == 0 || height == 0)
    {
        return false;
    }

    bx::Error error;
    bx::FileWriter writer;
    const bx::FilePath outputPath(filePath);
    if (!writer.open(outputPath, false, &error))
    {
        return false;
    }

    bx::DefaultAllocator allocator;
    const int32_t bytesWritten = bimg::imageWritePng(&writer, width, height, pitch, data,
                                                     static_cast<bimg::TextureFormat::Enum>(format),
                                                     yflip, &error);
    writer.close();
    return bytesWritten > 0 && error.isOk();
}

std::string makeTimestampedScreenshotPath()
{
    const auto now = std::chrono::system_clock::now();
    const auto timeValue = std::chrono::system_clock::to_time_t(now);
    const std::tm localNow = localTime(timeValue);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
            .count()
        % 1000;

    std::ostringstream fileName;
    fileName << "snapshot_" << std::put_time(&localNow, "%Y%m%d_%H%M%S")
             << '_' << std::setw(3) << std::setfill('0') << milliseconds << ".png";

    return (std::filesystem::current_path() / fileName.str()).string();
}