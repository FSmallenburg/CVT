#pragma once

#include <bgfx/bgfx.h>

#include <string>
#include <unordered_map>

class ScreenshotCallback final : public bgfx::CallbackI
{
  public:
    ScreenshotCallback() = default;

    void queueViewportCrop(const std::string &filePath, uint32_t viewportWidth);

    void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code,
               const char *_str) override;
    void traceVargs(const char *_filePath, uint16_t _line, const char *_format,
                    va_list _argList) override;
    void profilerBegin(const char *_name, uint32_t _abgr, const char *_filePath,
                       uint16_t _line) override;
    void profilerBeginLiteral(const char *_name, uint32_t _abgr,
                              const char *_filePath, uint16_t _line) override;
    void profilerEnd() override;
    uint32_t cacheReadSize(uint64_t _id) override;
    bool cacheRead(uint64_t _id, void *_data, uint32_t _size) override;
    void cacheWrite(uint64_t _id, const void *_data, uint32_t _size) override;
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;
    void captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch,
                      bgfx::TextureFormat::Enum _format, bool _yflip) override;
    void captureEnd() override;
    void captureFrame(const void *_data, uint32_t _size) override;

  private:
    bool writeScreenshot(const char *filePath, uint32_t width, uint32_t height,
                         uint32_t pitch, bgfx::TextureFormat::Enum format,
                         const void *data, bool yflip);

    uint32_t takeQueuedViewportCrop(const char *filePath);

    std::unordered_map<std::string, uint32_t> pendingViewportCrops;
};

std::string makeTimestampedScreenshotPath();