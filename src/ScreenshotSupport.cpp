#include "ScreenshotSupport.h"
#include "Log.h"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/file.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

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
    cvt::log::errorf("bgfx fatal %u at %s:%u: %s\n", static_cast<unsigned>(code),
                     filePath != nullptr ? filePath : "<unknown>",
                     static_cast<unsigned>(line),
                     message != nullptr ? message : "<no message>");
    if (code != bgfx::Fatal::DebugCheck)
    {
        std::abort();
    }
}

void ScreenshotCallback::traceVargs(const char *filePath, uint16_t line, const char *format,
                                    va_list argList)
{
    cvt::log::errorf("bgfx trace %s:%u: ", filePath != nullptr ? filePath : "<unknown>",
                     static_cast<unsigned>(line));
    cvt::log::verrorf(format, argList);
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

void ScreenshotCallback::queueViewportCrop(const std::string &filePath, uint32_t viewportWidth)
{
    if (!filePath.empty())
    {
        pendingViewportCrops[filePath] = viewportWidth;
    }
}

void ScreenshotCallback::screenShot(const char *filePath, uint32_t width, uint32_t height,
                                    uint32_t pitch, bgfx::TextureFormat::Enum format,
                                    const void *data, uint32_t, bool yflip)
{
    const uint32_t viewportWidth = takeQueuedViewportCrop(filePath);
    if (viewportWidth > 0 && viewportWidth < width && data != nullptr)
    {
        const uint32_t bytesPerPixel =
            bimg::getBitsPerPixel(static_cast<bimg::TextureFormat::Enum>(format)) / 8u;
        const uint32_t croppedPitch = viewportWidth * bytesPerPixel;
        if (bytesPerPixel > 0 && croppedPitch <= pitch)
        {
            std::vector<uint8_t> croppedData(static_cast<size_t>(croppedPitch) * height);
            const uint8_t *sourceBytes = static_cast<const uint8_t *>(data);
            for (uint32_t row = 0; row < height; ++row)
            {
                std::memcpy(croppedData.data() + static_cast<size_t>(row) * croppedPitch,
                            sourceBytes + static_cast<size_t>(row) * pitch,
                            croppedPitch);
            }

            if (!writeScreenshot(filePath, viewportWidth, height, croppedPitch, format,
                                 croppedData.data(), yflip))
            {
                cvt::log::errorf("Failed to write screenshot: %s\n",
                                 filePath != nullptr ? filePath : "<unknown>");
                return;
            }

            cvt::log::infof("Saved snapshot: %s\n",
                            filePath != nullptr ? filePath : "<unknown>");
            return;
        }
    }

    if (!writeScreenshot(filePath, width, height, pitch, format, data, yflip))
    {
        cvt::log::errorf("Failed to write screenshot: %s\n",
                         filePath != nullptr ? filePath : "<unknown>");
        return;
    }

    cvt::log::infof("Saved snapshot: %s\n", filePath != nullptr ? filePath : "<unknown>");
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

uint32_t ScreenshotCallback::takeQueuedViewportCrop(const char *filePath)
{
    if (filePath == nullptr)
    {
        return 0u;
    }

    const auto it = pendingViewportCrops.find(filePath);
    if (it == pendingViewportCrops.end())
    {
        return 0u;
    }

    const uint32_t viewportWidth = it->second;
    pendingViewportCrops.erase(it);
    return viewportWidth;
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