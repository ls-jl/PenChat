#include "image_data.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

namespace camera {

bool imageDataUrl(const std::string& path, std::string* output, std::string* error)
{
    output->clear();
    error->clear();
    if (path.empty() || path.find('\0') != std::string::npos) {
        *error = "invalid image path";
        return false;
    }
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        *error = "cannot open image";
        return false;
    }
    struct stat statbuf;
    if (fstat(fileno(file), &statbuf) != 0 || !S_ISREG(statbuf.st_mode) ||
        statbuf.st_size <= 0 || statbuf.st_size > 16 * 1024 * 1024) {
        std::fclose(file);
        *error = "image must be a nonempty regular file no larger than 16 MiB";
        return false;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(statbuf.st_size));
    const size_t count = std::fread(bytes.data(), 1, bytes.size(), file);
    const bool complete = count == bytes.size() && std::fgetc(file) == EOF && !std::ferror(file);
    std::fclose(file);
    if (!complete) {
        *error = "image changed or could not be read completely";
        return false;
    }
    const char* mime = 0;
    const unsigned char* p = bytes.data();
    if (count >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff) mime = "image/jpeg";
    else if (count >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) mime = "image/png";
    else if (count >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0) mime = "image/webp";
    else if (count >= 2 && p[0] == 'B' && p[1] == 'M') mime = "image/bmp";
    if (!mime) {
        *error = "unsupported image format";
        return false;
    }
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *output = std::string("data:") + mime + ";base64,";
    output->reserve(output->size() + ((count + 2) / 3) * 4);
    for (size_t i = 0; i < count; i += 3) {
        const unsigned int value = (static_cast<unsigned int>(p[i]) << 16) |
            (i + 1 < count ? static_cast<unsigned int>(p[i + 1]) << 8 : 0) |
            (i + 2 < count ? static_cast<unsigned int>(p[i + 2]) : 0);
        output->push_back(alphabet[(value >> 18) & 63]);
        output->push_back(alphabet[(value >> 12) & 63]);
        output->push_back(i + 1 < count ? alphabet[(value >> 6) & 63] : '=');
        output->push_back(i + 2 < count ? alphabet[value & 63] : '=');
    }
    return true;
}

}  // namespace camera
