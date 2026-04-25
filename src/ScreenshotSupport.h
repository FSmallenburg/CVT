#pragma once

#include <bgfx/bgfx.h>

#include <string>
#include <unordered_map>

/// bgfx callback implementation that handles screenshot saving, bgfx error
/// reporting, and video capture stubs. Pass a pointer to this object to
/// bgfx::Init::callback before calling bgfx::init().
class ScreenshotCallback final : public bgfx::CallbackI
{
  public:
    ScreenshotCallback() = default;

    /// Registers a viewport crop for the next screenshot saved to @p filePath.
    /// When bgfx calls screenShot() for that path, the image is cropped to
    /// @p viewportWidth pixels wide before being written. The crop entry is
    /// consumed once and then discarded.
    void queueViewportCrop(const std::string &filePath, uint32_t viewportWidth);

    /// bgfx fatal-error callback. Logs the error and aborts unless the code is
    /// DebugCheck, which only logs.
    void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code,
               const char *_str) override;

    /// bgfx trace/log callback. Forwards the printf-style message to stderr.
    void traceVargs(const char *_filePath, uint16_t _line, const char *_format,
                    va_list _argList) override;

    // Profiler callbacks — not implemented; these are no-ops.
    void profilerBegin(const char *_name, uint32_t _abgr, const char *_filePath,
                       uint16_t _line) override;
    void profilerBeginLiteral(const char *_name, uint32_t _abgr,
                              const char *_filePath, uint16_t _line) override;
    void profilerEnd() override;

    // Shader-cache callbacks — not implemented; cache is always empty.
    uint32_t cacheReadSize(uint64_t _id) override;
    bool cacheRead(uint64_t _id, void *_data, uint32_t _size) override;
    void cacheWrite(uint64_t _id, const void *_data, uint32_t _size) override;

    /// bgfx screenshot callback. Writes a PNG file to @p _filePath. If a
    /// viewport crop was previously queued for this path, the image is cropped
    /// to that width before writing.
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;

    // Video-capture callbacks — not implemented; these are no-ops.
    void captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch,
                      bgfx::TextureFormat::Enum _format, bool _yflip) override;
    void captureEnd() override;
    void captureFrame(const void *_data, uint32_t _size) override;

  private:
    /// Encodes @p data as a PNG and writes it to @p filePath.
    /// Returns false if the file could not be opened or the write failed.
    bool writeScreenshot(const char *filePath, uint32_t width, uint32_t height,
                         uint32_t pitch, bgfx::TextureFormat::Enum format,
                         const void *data, bool yflip);

    /// Returns the pending viewport-crop width for @p filePath and removes the
    /// entry, or returns 0 if no crop was queued for that path.
    uint32_t takeQueuedViewportCrop(const char *filePath);

    /// Map from screenshot file path to the desired cropped viewport width.
    std::unordered_map<std::string, uint32_t> pendingViewportCrops;
};

/// Returns a timestamped screenshot path of the form
/// "snapshot_YYYYMMDD_HHMMSS_mmm.png". If @p loadedPath is a file path, the
/// screenshot is written next to that file; otherwise it falls back to the
/// current working directory.
std::string makeTimestampedScreenshotPath(const std::string &loadedPath);