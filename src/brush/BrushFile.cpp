#include "brush/BrushFile_p.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace komapaint::brush {
namespace {

namespace fs = std::filesystem;

bool failed(std::string* error, std::string_view message) {
    if (error) *error = message;
    return false;
}

// Exclusive creation is important even with a unique-looking name: another
// save must never open, truncate, or clean up our staged file.
class StagedBrush {
public:
    StagedBrush() = default;
    StagedBrush(const StagedBrush&) = delete;
    StagedBrush& operator=(const StagedBrush&) = delete;
    ~StagedBrush() {
        if (m_file) std::fclose(m_file);
        if (!m_path.empty()) {
            std::error_code ignored;
            fs::remove(m_path, ignored);
        }
    }

    bool open(const fs::path& directory) {
        static std::atomic<std::uint64_t> sequence{0};
        const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 128; ++attempt) {
            auto path = directory / (".komapaint-brush-" + std::to_string(time) + '-' +
                                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp");
#ifdef _WIN32
            int fd = -1;
            const int result = _wsopen_s(&fd, path.c_str(), _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
                                         _SH_DENYRW, _S_IREAD | _S_IWRITE);
#else
            const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
            const int result = fd >= 0 ? 0 : errno;
#endif
            if (result == EEXIST) continue;
            if (result != 0) return false;
            m_path = std::move(path);
#ifdef _WIN32
            m_file = _fdopen(fd, "r+b");
            if (!m_file) _close(fd);
#else
            m_file = fdopen(fd, "r+b");
            if (!m_file) ::close(fd);
#endif
            return m_file != nullptr;
        }
        return false;
    }

    std::FILE* file() const { return m_file; }
    const fs::path& path() const { return m_path; }

    bool sync() const {
        if (std::fflush(m_file) != 0) return false;
#ifdef _WIN32
        return _commit(_fileno(m_file)) == 0;
#else
        return ::fsync(fileno(m_file)) == 0;
#endif
    }

    bool close() { return std::fclose(std::exchange(m_file, nullptr)) == 0; }
    void committed() { m_path.clear(); }

private:
    fs::path m_path;
    std::FILE* m_file = nullptr;
};

bool matchesFile(std::FILE* file, const std::vector<std::uint8_t>& bytes) {
    if (std::fseek(file, 0, SEEK_SET) != 0) return false;
    std::array<std::uint8_t, 16384> block;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const std::size_t count = std::min(block.size(), bytes.size() - offset);
        if (std::fread(block.data(), 1, count, file) != count ||
            std::memcmp(block.data(), bytes.data() + offset, count) != 0) return false;
        offset += count;
    }
    return std::fgetc(file) == EOF && !std::ferror(file);
}

} // namespace

bool detail::saveBrushFile(const fs::path& path, const BrushDocument& document,
                           std::string* error, SaveFault fault) {
    if (error) error->clear();
    try {
        if (path.empty()) return failed(error, "No brush filename was given.");
        const auto validation = document.validate();
        if (!validation.ok()) {
            return failed(error, "This brush cannot be saved yet: " + validation.summary());
        }
        const auto bytes = encodeBrush(document);
        const auto destination = fs::absolute(path);
        StagedBrush staged;
        if (!staged.open(destination.parent_path())) {
            return failed(error, "Could not create a temporary brush file beside the destination.");
        }
        const std::size_t count = bytes.size() - (fault == SaveFault::ShortWrite ? 1u : 0u);
        if (std::fwrite(bytes.data(), 1, count, staged.file()) != bytes.size() || !staged.sync()) {
            return failed(error, "Could not finish writing the temporary brush file. Check free space and permissions.");
        }
        if (fault == SaveFault::BadReadback) {
            // Corrupt the staged output, not the original and not the expected
            // in-memory bytes, so the real readback check must catch it.
            if (std::fseek(staged.file(), 0, SEEK_SET) != 0 ||
                std::fputc(bytes.front() ^ 1, staged.file()) == EOF || !staged.sync()) {
                return failed(error, "Could not corrupt the test's temporary file.");
            }
        }
        if (!matchesFile(staged.file(), bytes) || !staged.close()) {
            return failed(error, "The temporary brush file did not match what was written.");
        }
        BrushDocument readBack;
        DecodeResult report;
        if (!decodeBrush(bytes.data(), bytes.size(), &readBack, &report)) {
            return failed(error, "Could not verify the completed brush file: " + report.error);
        }
        if (readBack != document) return failed(error, "The completed brush file changed the brush data.");

        // Source and destination are siblings: rename replaces a regular file
        // atomically on supported filesystems. Never delete the destination or
        // fall back to writing into it, including on platforms that refuse it.
        std::error_code ec;
        if (fault == SaveFault::Replace) ec = std::make_error_code(std::errc::permission_denied);
        else fs::rename(staged.path(), destination, ec);
        if (ec) return failed(error, "Could not replace the brush file: " + ec.message());
        staged.committed();
        return true;
    } catch (const std::bad_alloc&) {
        return failed(error, "Not enough memory to save the brush file.");
    } catch (const std::length_error&) {
        return failed(error, "The brush file is too large to write on this system.");
    } catch (const std::system_error& cause) {
        return failed(error, "Could not save the brush file: " + cause.code().message());
    }
}

bool saveBrushFile(const fs::path& path, const BrushDocument& document, std::string* error) {
    return detail::saveBrushFile(path, document, error, detail::SaveFault::None);
}

bool loadBrushFile(const fs::path& path, BrushDocument* out, DecodeResult* result, std::string* error) {
    if (error) error->clear();
    if (result) *result = {};
    try {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return failed(error, "Could not open the brush file.");
        const auto length = stream.tellg();
        if (length < 0 || static_cast<std::uintmax_t>(length) > std::vector<std::uint8_t>{}.max_size() ||
            length > std::numeric_limits<std::streamsize>::max()) {
            return failed(error, "Could not determine a readable size for the brush file.");
        }
        stream.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))) {
            return failed(error, "Could not finish reading the brush file.");
        }
        DecodeResult local;
        auto* report = result ? result : &local;
        if (!decodeBrush(bytes.data(), bytes.size(), out, report)) return failed(error, report->error);
        return true;
    } catch (const std::bad_alloc&) {
        return failed(error, "Not enough memory to load the brush file.");
    } catch (const std::length_error&) {
        return failed(error, "The brush file is too large to read on this system.");
    } catch (const std::system_error& cause) {
        return failed(error, "Could not read the brush file: " + cause.code().message());
    }
}

} // namespace komapaint::brush
