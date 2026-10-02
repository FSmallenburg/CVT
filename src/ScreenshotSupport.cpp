#include "ScreenshotSupport.h"
#include "Log.h"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>

#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
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

std::string normalizeScreenshotPathKey(const std::string &path)
{
    std::string normalized = path;
    for (char &ch : normalized)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
#if defined(_WIN32)
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return normalized;
}

/// Encodes @p data as a PNG and writes it to @p filePath.
/// Returns false if the file could not be opened or the write failed.
bool writePngFile(const char *filePath, uint32_t width, uint32_t height, uint32_t pitch,
                  bgfx::TextureFormat::Enum format, const void *data, bool yflip)
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

    const int32_t bytesWritten = bimg::imageWritePng(&writer, width, height, pitch, data,
                                                     static_cast<bimg::TextureFormat::Enum>(format),
                                                     yflip, &error);
    writer.close();
    return bytesWritten > 0 && error.isOk();
}

/// One source sample contributing to an output pixel along a single axis.
struct ResampleTap
{
    uint32_t source;
    float weight;
};

/// For each of @p outputSize output pixels, lists the source pixels it covers
/// when @p sourceSize pixels are shrunk to @p outputSize, weighted by overlap.
/// Weights per output pixel sum to 1. Requires outputSize <= sourceSize.
std::vector<std::vector<ResampleTap>> buildAreaResampleTaps(uint32_t sourceSize,
                                                            uint32_t outputSize)
{
    std::vector<std::vector<ResampleTap>> taps(outputSize);
    const double ratio = double(sourceSize) / double(outputSize);
    for (uint32_t output = 0; output < outputSize; ++output)
    {
        const double start = output * ratio;
        const double end = std::min(double(sourceSize), (output + 1) * ratio);
        for (uint32_t source = uint32_t(start); source < sourceSize && source < end; ++source)
        {
            const double overlap = std::min(end, source + 1.0) - std::max(start, double(source));
            if (overlap > 0.0)
            {
                taps[output].push_back({source, float(overlap / ratio)});
            }
        }
    }
    return taps;
}

/// Shrinks a 4-byte-per-pixel image to @p outputWidth x @p outputHeight by
/// area averaging each 8-bit channel. Returns a tightly packed buffer.
std::vector<uint8_t> areaDownsample(const uint8_t *source, uint32_t sourceWidth,
                                    uint32_t sourceHeight, uint32_t sourcePitch,
                                    uint32_t outputWidth, uint32_t outputHeight)
{
    constexpr uint32_t kChannels = 4u;
    const auto columnTaps = buildAreaResampleTaps(sourceWidth, outputWidth);
    const auto rowTaps = buildAreaResampleTaps(sourceHeight, outputHeight);

    // Horizontal pass: every source row shrunk to outputWidth.
    std::vector<float> horizontal(size_t(outputWidth) * sourceHeight * kChannels, 0.0f);
    for (uint32_t row = 0; row < sourceHeight; ++row)
    {
        const uint8_t *sourceRow = source + size_t(row) * sourcePitch;
        float *targetRow = horizontal.data() + size_t(row) * outputWidth * kChannels;
        for (uint32_t column = 0; column < outputWidth; ++column)
        {
            float *target = targetRow + column * kChannels;
            for (const ResampleTap &tap : columnTaps[column])
            {
                const uint8_t *pixel = sourceRow + tap.source * kChannels;
                for (uint32_t channel = 0; channel < kChannels; ++channel)
                {
                    target[channel] += tap.weight * pixel[channel];
                }
            }
        }
    }

    // Vertical pass.
    const size_t outputRowFloats = size_t(outputWidth) * kChannels;
    std::vector<uint8_t> output(outputRowFloats * outputHeight);
    std::vector<float> accumulator(outputRowFloats);
    for (uint32_t row = 0; row < outputHeight; ++row)
    {
        std::fill(accumulator.begin(), accumulator.end(), 0.0f);
        for (const ResampleTap &tap : rowTaps[row])
        {
            const float *sourceRow = horizontal.data() + size_t(tap.source) * outputRowFloats;
            for (size_t index = 0; index < outputRowFloats; ++index)
            {
                accumulator[index] += tap.weight * sourceRow[index];
            }
        }
        uint8_t *targetRow = output.data() + size_t(row) * outputRowFloats;
        for (size_t index = 0; index < outputRowFloats; ++index)
        {
            targetRow[index] =
                static_cast<uint8_t>(std::clamp(accumulator[index] + 0.5f, 0.0f, 255.0f));
        }
    }
    return output;
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

void ScreenshotCallback::queueScreenshot(const std::string &filePath, uint32_t viewportWidth,
                                         float scale)
{
    if (!filePath.empty())
    {
        pendingScreenshots[normalizeScreenshotPathKey(filePath)] =
            ScreenshotRequest{viewportWidth, scale};
    }
}

void ScreenshotCallback::screenShot(const char *filePath, uint32_t width, uint32_t height,
                                    uint32_t pitch, bgfx::TextureFormat::Enum format,
                                    const void *data, uint32_t, bool yflip)
{
    const ScreenshotRequest request = takeQueuedScreenshot(filePath);
    cvt::log::infof("Screenshot callback: file=%s full=%ux%u cropWidth=%u scale=%g pitch=%u\n",
                    filePath != nullptr ? filePath : "<unknown>",
                    static_cast<unsigned>(width),
                    static_cast<unsigned>(height),
                    static_cast<unsigned>(request.viewportWidth),
                    static_cast<double>(request.scale),
                    static_cast<unsigned>(pitch));

    const uint32_t bytesPerPixel =
        bimg::getBitsPerPixel(static_cast<bimg::TextureFormat::Enum>(format)) / 8u;
    const uint8_t *imageBytes = static_cast<const uint8_t *>(data);
    uint32_t imageWidth = width;
    uint32_t imageHeight = height;
    uint32_t imagePitch = pitch;

    // Cropping only narrows the region read from each row; no copy is needed.
    if (request.viewportWidth > 0 && request.viewportWidth < width && data != nullptr
        && bytesPerPixel > 0 && request.viewportWidth * bytesPerPixel <= pitch)
    {
        imageWidth = request.viewportWidth;
    }

    std::vector<uint8_t> downsampledData;
    if (request.scale < 1.0f && data != nullptr)
    {
        const uint32_t outputWidth =
            std::max(1u, static_cast<uint32_t>(std::lround(imageWidth * request.scale)));
        const uint32_t outputHeight =
            std::max(1u, static_cast<uint32_t>(std::lround(imageHeight * request.scale)));
        if (bytesPerPixel != 4u)
        {
            cvt::log::errorf("Screenshot downsampling unsupported for format %u; "
                             "saving at full resolution\n",
                             static_cast<unsigned>(format));
        }
        else if (outputWidth < imageWidth || outputHeight < imageHeight)
        {
            downsampledData = areaDownsample(imageBytes, imageWidth, imageHeight, imagePitch,
                                             outputWidth, outputHeight);
            imageBytes = downsampledData.data();
            imageWidth = outputWidth;
            imageHeight = outputHeight;
            imagePitch = outputWidth * bytesPerPixel;
        }
    }

    if (!writePngFile(filePath, imageWidth, imageHeight, imagePitch, format, imageBytes, yflip))
    {
        cvt::log::errorf("Failed to write screenshot: %s\n",
                         filePath != nullptr ? filePath : "<unknown>");
        return;
    }

    cvt::log::infof("Saved snapshot (%ux%u): %s\n",
                    static_cast<unsigned>(imageWidth),
                    static_cast<unsigned>(imageHeight),
                    filePath != nullptr ? filePath : "<unknown>");
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

ScreenshotCallback::ScreenshotRequest
ScreenshotCallback::takeQueuedScreenshot(const char *filePath)
{
    if (filePath == nullptr)
    {
        return {};
    }

    const std::string normalizedPath = normalizeScreenshotPathKey(filePath);
    const auto it = pendingScreenshots.find(normalizedPath);
    if (it == pendingScreenshots.end())
    {
        return {};
    }

    const ScreenshotRequest request = it->second;
    pendingScreenshots.erase(it);
    return request;
}

bool OffscreenScreenshot::begin(const std::string &filePath, uint16_t width,
                                uint16_t height)
{
    if (active())
    {
        cvt::log::errorf("Screenshot skipped: previous high-resolution screenshot "
                         "is still being saved\n");
        return false;
    }

    const bgfx::Caps *caps = bgfx::getCaps();
    if ((caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) == 0
        || (caps->supported & BGFX_CAPS_TEXTURE_BLIT) == 0)
    {
        cvt::log::errorf("High-resolution screenshot unsupported: "
                         "texture readback/blit unavailable\n");
        return false;
    }

    constexpr uint64_t kColorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP
                                     | BGFX_SAMPLER_V_CLAMP;
    constexpr uint64_t kReadbackFlags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK
                                        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    constexpr uint64_t kDepthFlags = BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_SAMPLER_U_CLAMP
                                     | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::BGRA8;
    if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags)
        || !bgfx::isTextureValid(0, false, 1, colorFormat, kReadbackFlags))
    {
        colorFormat = bgfx::TextureFormat::RGBA8;
        if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags)
            || !bgfx::isTextureValid(0, false, 1, colorFormat, kReadbackFlags))
        {
            cvt::log::errorf("High-resolution screenshot unsupported: "
                             "no RGBA8/BGRA8 render target\n");
            return false;
        }
    }

    bgfx::TextureFormat::Enum depthFormat = bgfx::TextureFormat::D24S8;
    if (!bgfx::isTextureValid(0, false, 1, depthFormat, kDepthFlags))
    {
        depthFormat = bgfx::TextureFormat::D32F;
        if (!bgfx::isTextureValid(0, false, 1, depthFormat, kDepthFlags))
        {
            cvt::log::errorf("High-resolution screenshot unsupported: "
                             "no depth render target\n");
            return false;
        }
    }

    m_colorTexture = bgfx::createTexture2D(width, height, false, 1, colorFormat, kColorFlags);
    m_readbackTexture =
        bgfx::createTexture2D(width, height, false, 1, colorFormat, kReadbackFlags);
    m_depthTexture = bgfx::createTexture2D(width, height, false, 1, depthFormat, kDepthFlags);
    if (bgfx::isValid(m_colorTexture) && bgfx::isValid(m_depthTexture))
    {
        bgfx::Attachment attachments[2];
        attachments[0].init(m_colorTexture);
        attachments[1].init(m_depthTexture);
        m_frameBuffer = bgfx::createFrameBuffer(2, attachments, false);
    }
    if (!bgfx::isValid(m_frameBuffer) || !bgfx::isValid(m_readbackTexture))
    {
        cvt::log::errorf("High-resolution screenshot failed: could not create %ux%u "
                         "render targets\n",
                         static_cast<unsigned>(width), static_cast<unsigned>(height));
        cancel();
        return false;
    }

    m_filePath = filePath;
    m_colorFormat = colorFormat;
    m_width = width;
    m_height = height;
    m_readbackData.resize(size_t(width) * height * 4u);
    m_renderPending = true;
    m_readbackPending = false;
    return true;
}

bool OffscreenScreenshot::active() const
{
    return bgfx::isValid(m_frameBuffer);
}

bool OffscreenScreenshot::renderPending() const
{
    return m_renderPending;
}

bgfx::FrameBufferHandle OffscreenScreenshot::frameBuffer() const
{
    return m_frameBuffer;
}

uint16_t OffscreenScreenshot::width() const
{
    return m_width;
}

uint16_t OffscreenScreenshot::height() const
{
    return m_height;
}

void OffscreenScreenshot::requestReadback(bgfx::ViewId blitView)
{
    if (!m_renderPending)
    {
        return;
    }

    bgfx::blit(blitView, m_readbackTexture, 0, 0, m_colorTexture);
    m_readbackFrame = bgfx::readTexture(m_readbackTexture, m_readbackData.data());
    m_renderPending = false;
    m_readbackPending = true;
}

void OffscreenScreenshot::poll(uint32_t submittedFrame)
{
    if (!m_readbackPending || submittedFrame < m_readbackFrame)
    {
        return;
    }

    const bool yflip = bgfx::getCaps()->originBottomLeft;
    if (writePngFile(m_filePath.c_str(), m_width, m_height, uint32_t(m_width) * 4u,
                     m_colorFormat, m_readbackData.data(), yflip))
    {
        cvt::log::infof("Saved snapshot (%ux%u): %s\n", static_cast<unsigned>(m_width),
                        static_cast<unsigned>(m_height), m_filePath.c_str());
    }
    else
    {
        cvt::log::errorf("Failed to write screenshot: %s\n", m_filePath.c_str());
    }
    cancel();
}

void OffscreenScreenshot::cancel()
{
    if (bgfx::isValid(m_frameBuffer))
    {
        bgfx::destroy(m_frameBuffer);
    }
    if (bgfx::isValid(m_colorTexture))
    {
        bgfx::destroy(m_colorTexture);
    }
    if (bgfx::isValid(m_depthTexture))
    {
        bgfx::destroy(m_depthTexture);
    }
    if (bgfx::isValid(m_readbackTexture))
    {
        bgfx::destroy(m_readbackTexture);
    }
    m_frameBuffer = BGFX_INVALID_HANDLE;
    m_colorTexture = BGFX_INVALID_HANDLE;
    m_depthTexture = BGFX_INVALID_HANDLE;
    m_readbackTexture = BGFX_INVALID_HANDLE;
    m_readbackData.clear();
    m_readbackData.shrink_to_fit();
    m_renderPending = false;
    m_readbackPending = false;
    m_width = 0;
    m_height = 0;
}

float OffscreenScreenshot::maxScale(uint16_t width, uint16_t height)
{
    const uint32_t largestSide = std::max<uint32_t>({width, height, 1u});
    const uint32_t textureLimit =
        std::min<uint32_t>(bgfx::getCaps()->limits.maxTextureSize, UINT16_MAX);
    return float(textureLimit) / float(largestSide);
}

std::string makeTimestampedScreenshotPath(const std::string &loadedPath,
                                          bool nextToLoadedFile)
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

    std::filesystem::path outputDirectory = std::filesystem::current_path();
    if (nextToLoadedFile && !loadedPath.empty())
    {
        std::error_code error;
        std::filesystem::path loadedFsPath(loadedPath);
        if (std::filesystem::is_directory(loadedFsPath, error))
        {
            outputDirectory = loadedFsPath;
        }
        else if (loadedFsPath.has_parent_path())
        {
            outputDirectory = loadedFsPath.parent_path();
        }
    }

    return (outputDirectory / fileName.str()).string();
}