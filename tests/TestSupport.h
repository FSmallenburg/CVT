#pragma once

#include <filesystem>
#include <fstream>
#include <string>

/// Writes @p contents to a file named @p fileName in a per-run temporary
/// directory and removes it again when the object goes out of scope.
class TemporaryFile
{
  public:
    TemporaryFile(const std::string &fileName, const std::string &contents)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / "cvt_tests";
        std::filesystem::create_directories(directory);
        m_path = directory / fileName;
        std::ofstream output(m_path, std::ios::binary);
        output << contents;
    }

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;

    std::string path() const { return m_path.string(); }

  private:
    std::filesystem::path m_path;
};
