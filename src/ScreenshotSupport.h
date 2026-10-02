#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// bgfx callback implementation that handles screenshot saving, bgfx error
/// reporting, and video capture stubs. Pass a pointer to this object to
/// bgfx::Init::callback before calling bgfx::init().
class ScreenshotCallback final : public bgfx::CallbackI
{
  public:
    ScreenshotCallback() = default;

    /// Registers post-processing for the next screenshot saved to @p filePath.
    /// When bgfx calls screenShot() for that path, the image is cropped to
    /// @p viewportWidth pixels wide and then, if @p scale < 1, downsampled to
    /// @p scale times its size in both directions by area averaging. Scales
    /// above 1 are ignored here; use OffscreenScreenshot for those. The entry
    /// is consumed once and then discarded.
    void queueScreenshot(const std::string &filePath, uint32_t viewportWidth, float scale);

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
    /// crop/downsample request was previously queued for this path, it is
    /// applied before writing.
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;

    // Video-capture callbacks — not implemented; these are no-ops.
    void captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch,
                      bgfx::TextureFormat::Enum _format, bool _yflip) override;
    void captureEnd() override;
    void captureFrame(const void *_data, uint32_t _size) override;

  private:
    /// Post-processing requested for a single screenshot.
    struct ScreenshotRequest
    {
        /// Cropped viewport width in pixels (0 = no crop).
        uint32_t viewportWidth = 0u;
        /// Output size relative to the cropped image (1 = full resolution).
        float scale = 1.0f;
    };

    /// Returns the pending request for @p filePath and removes the entry, or
    /// returns a default (no crop, full resolution) request if none was queued.
    ScreenshotRequest takeQueuedScreenshot(const char *filePath);

    /// Map from screenshot file path to the requested post-processing.
    std::unordered_map<std::string, ScreenshotRequest> pendingScreenshots;
};

/// Renders a screenshot into an offscreen framebuffer, so it can be larger than
/// the window, and writes it to a PNG once the GPU readback completes.
///
/// Per screenshot: begin() allocates the targets; while renderPending() is
/// true the caller renders the scene into frameBuffer() and then calls
/// requestReadback(); after every bgfx::frame() the caller passes the returned
/// frame number to poll(), which writes the file and releases the targets.
class OffscreenScreenshot
{
  public:
    OffscreenScreenshot() = default;
    OffscreenScreenshot(const OffscreenScreenshot &) = delete;
    OffscreenScreenshot &operator=(const OffscreenScreenshot &) = delete;

    /// Allocates @p width x @p height render targets for a screenshot written to
    /// @p filePath. Returns false (and logs why) if a screenshot is already in
    /// progress or the GPU cannot provide the targets.
    bool begin(const std::string &filePath, uint16_t width, uint16_t height);

    /// True between begin() and the PNG being written (or cancel()).
    bool active() const;
    /// True when the scene still has to be rendered into frameBuffer().
    bool renderPending() const;

    bgfx::FrameBufferHandle frameBuffer() const;
    uint16_t width() const;
    uint16_t height() const;

    /// Queues the copy of the rendered image to CPU memory. @p blitView must be
    /// ordered after the view the scene was rendered into.
    void requestReadback(bgfx::ViewId blitView);

    /// Writes the PNG and releases the targets once the frame numbered
    /// @p submittedFrame has delivered the readback data.
    void poll(uint32_t submittedFrame);

    /// Releases all GPU resources without writing anything.
    void cancel();

    /// Largest scale for which a @p width x @p height viewport still fits in a
    /// single GPU texture.
    static float maxScale(uint16_t width, uint16_t height);

  private:
    std::string m_filePath;
    bgfx::TextureHandle m_colorTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_depthTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_readbackTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum m_colorFormat = bgfx::TextureFormat::Count;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    std::vector<uint8_t> m_readbackData;
    bool m_renderPending = false;
    bool m_readbackPending = false;
    uint32_t m_readbackFrame = 0;
};

/// Returns a timestamped screenshot path of the form
/// "snapshot_YYYYMMDD_HHMMSS_mmm.png". The file goes in the current working
/// directory, unless @p nextToLoadedFile is set and @p loadedPath names a file
/// or directory, in which case it is written next to that file (or inside
/// that directory).
std::string makeTimestampedScreenshotPath(const std::string &loadedPath,
                                          bool nextToLoadedFile);