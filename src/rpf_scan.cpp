#include "rpf_scan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr uint32_t RPF_MAGIC = 0x52504637U;
constexpr uint32_t RSC7_MAGIC = 0x37435352U;
constexpr uint32_t RPF_BLOCK_SIZE = 512U;
constexpr uint32_t NONE_ENCRYPTION = 0U;
constexpr uint32_t OPEN_ENCRYPTION = 0x4E45504FU;
constexpr uint32_t AES_ENCRYPTION = 0x0FFFFFF9U;
constexpr uint32_t NG_ENCRYPTION = 0x0FEFFFFFU;
constexpr size_t NG_KEYS_SIZE = 27472U;

enum class EntryType { Directory, Binary, Resource };

struct Entry {
    EntryType type = EntryType::Binary;
    std::string name;
    uint32_t entries_index = 0;
    uint32_t entries_count = 0;
    uint32_t file_offset = 0;
    uint32_t file_size = 0;
    uint32_t file_uncompressed_size = 0;
    uint32_t system_flags = 0;
    uint32_t graphics_flags = 0;
};

struct Reader {
    explicit Reader(const wchar_t *path) : stream(path, std::ios::binary) {
        if (!stream) throw std::runtime_error("failed to open RPF");
        stream.seekg(0, std::ios::end);
        size = static_cast<uint64_t>(stream.tellg());
    }

    std::vector<uint8_t> read(uint64_t offset, size_t count) {
        if (offset > size || count > size - offset)
            throw std::runtime_error("truncated RPF entry");
        std::vector<uint8_t> out(count);
        if (count == 0) return out;
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        stream.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(count));
        if (static_cast<size_t>(stream.gcount()) != count)
            throw std::runtime_error("failed to read RPF entry");
        return out;
    }

    std::ifstream stream;
    uint64_t size = 0;
};

struct Archive {
    uint64_t base_offset = 0;
    uint64_t size = 0;
    std::wstring prefix;
    std::string name;
};

uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}

uint64_t rd64(const uint8_t *p) {
    return static_cast<uint64_t>(rd32(p)) |
           (static_cast<uint64_t>(rd32(p + 4)) << 32U);
}

void wr32(uint32_t value, uint8_t *p) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8U);
    p[2] = static_cast<uint8_t>(value >> 16U);
    p[3] = static_cast<uint8_t>(value >> 24U);
}

std::vector<uint8_t> read_auxiliary_file(const wchar_t *name) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return {};
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) return {};
    slash[1] = 0;
    wcsncat(path, name, MAX_PATH - wcslen(path) - 1);
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    std::vector<uint8_t> out(static_cast<size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
    return out;
}

uint32_t jenk_hash(const std::string &value, const std::vector<uint8_t> &lut) {
    uint32_t result = 0;
    for (unsigned char byte : value) {
        const uint32_t temp = 1025U * (static_cast<uint8_t>(lut[byte]) + result);
        result = (temp >> 6U) ^ temp;
    }
    const uint32_t tail = 9U * result;
    return 32769U * ((tail >> 11U) ^ tail);
}

struct Crypto {
    Crypto() : ng_blob(read_auxiliary_file(L"ng.dat")), lut(read_auxiliary_file(L"lut.dat")) {}

    std::vector<uint8_t> decrypt(std::vector<uint8_t> data, uint32_t encryption,
                                 const std::string &archive_name, uint32_t archive_size) const {
        if (encryption == NONE_ENCRYPTION || encryption == OPEN_ENCRYPTION) return data;
        if (encryption == AES_ENCRYPTION) return decrypt_aes(std::move(data));
        if (encryption != NG_ENCRYPTION) throw std::runtime_error("unsupported RPF encryption");
        if (ng_blob.size() < NG_KEYS_SIZE + 278528U || lut.size() != 256U)
            throw std::runtime_error("NG-encrypted RPF requires ng.dat and lut.dat beside the executable");
        return decrypt_ng(std::move(data), archive_name, archive_size);
    }

    std::vector<uint8_t> decrypt_aes(std::vector<uint8_t> data) const {
        static const uint8_t key[32] = {
            0xB3,0x89,0x73,0xAF,0x8B,0x9E,0x26,0x3A,0x8D,0xF1,0x70,0x32,0x14,0x42,0xB3,0x93,
            0x8B,0xD3,0xF2,0x1F,0xA4,0xD0,0x4D,0xFF,0x88,0x2E,0x04,0x66,0x0F,0xF9,0x9D,0xFD
        };
        const size_t aligned = data.size() - data.size() % 16U;
        if (!aligned) return data;
        BCRYPT_ALG_HANDLE algorithm = NULL;
        BCRYPT_KEY_HANDLE handle = NULL;
        DWORD object_size = 0, written = 0;
        std::vector<uint8_t> key_object;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, NULL, 0) < 0)
            throw std::runtime_error("failed to initialize AES");
        const wchar_t mode[] = BCRYPT_CHAIN_MODE_ECB;
        if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)mode, sizeof(mode), 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size, sizeof(object_size), &written, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("failed to configure AES");
        }
        key_object.resize(object_size);
        if (BCryptGenerateSymmetricKey(algorithm, &handle, key_object.data(), object_size,
                                       (PUCHAR)key, sizeof(key), 0) < 0 ||
            BCryptDecrypt(handle, data.data(), static_cast<ULONG>(aligned), NULL, NULL, 0,
                          data.data(), static_cast<ULONG>(aligned), &written, 0) < 0) {
            if (handle) BCryptDestroyKey(handle);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("failed to decrypt AES RPF table");
        }
        BCryptDestroyKey(handle);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return data;
    }

    uint32_t table(size_t base, uint8_t index) const {
        return rd32(ng_blob.data() + NG_KEYS_SIZE + (base + index) * 4U);
    }

    void round(uint8_t *block, size_t key_base, size_t round_index, bool shuffle) const {
        const size_t t = round_index * 16U * 256U;
        const size_t indexes[16] = {
            0U, shuffle ? 7U : 1U, shuffle ? 10U : 2U, shuffle ? 13U : 3U,
            shuffle ? 1U : 4U, 4U, shuffle ? 11U : 6U, shuffle ? 14U : 7U,
            shuffle ? 2U : 8U, shuffle ? 5U : 9U, shuffle ? 8U : 10U, shuffle ? 15U : 11U,
            shuffle ? 3U : 12U, shuffle ? 6U : 13U, shuffle ? 9U : 14U, shuffle ? 12U : 15U
        };
        uint32_t out[4];
        for (size_t row = 0; row < 4; ++row) {
            out[row] = rd32(ng_blob.data() + key_base + row * 4U);
            for (size_t col = 0; col < 4; ++col)
                out[row] ^= table(t + (row * 4U + col) * 256U, block[indexes[row * 4U + col]]);
        }
        for (size_t i = 0; i < 4; ++i) wr32(out[i], block + i * 4U);
    }

    std::vector<uint8_t> decrypt_ng(std::vector<uint8_t> data, const std::string &name,
                                    uint32_t archive_size) const {
        const size_t key_index = (jenk_hash(name, lut) + archive_size + 61U) % 0x65U;
        const size_t base = key_index * 272U;
        const size_t aligned = data.size() - data.size() % 16U;
        for (size_t offset = 0; offset < aligned; offset += 16U) {
            round(data.data() + offset, base, 0, false);
            round(data.data() + offset, base + 16U, 1, false);
            for (size_t r = 2; r < 16; ++r) round(data.data() + offset, base + r * 16U, r, true);
            round(data.data() + offset, base + 16U * 16U, 16, false);
        }
        return data;
    }

    std::vector<uint8_t> ng_blob;
    std::vector<uint8_t> lut;
};

uint32_t resource_size_from_flags(uint32_t flags) {
    const uint32_t count =
        (((flags >> 27U) & 1U) << 0U) + (((flags >> 26U) & 1U) << 1U) +
        (((flags >> 25U) & 1U) << 2U) + (((flags >> 24U) & 1U) << 3U) +
        (((flags >> 17U) & 0x7FU) << 4U) + (((flags >> 11U) & 0x3FU) << 5U) +
        (((flags >> 7U) & 0xFU) << 6U) + (((flags >> 5U) & 0x3U) << 7U) +
        (((flags >> 4U) & 1U) << 8U);
    return (0x200U << (flags & 0xFU)) * count;
}

std::string read_name(const std::vector<uint8_t> &names, uint32_t offset) {
    if (offset >= names.size()) return {};
    const char *begin = reinterpret_cast<const char *>(names.data() + offset);
    const size_t available = names.size() - offset;
    const char *end = static_cast<const char *>(std::memchr(begin, 0, available));
    return end ? std::string(begin, static_cast<size_t>(end - begin))
               : std::string(begin, available);
}

std::wstring widen(const std::string &value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        &out[0], count);
    return out;
}

bool ends_with_ci(const std::string &value, const char *suffix) {
    const size_t n = std::strlen(suffix);
    if (value.size() < n) return false;
    return _stricmp(value.c_str() + value.size() - n, suffix) == 0;
}

bool is_supported(const std::string &name) {
    return ends_with_ci(name, ".ytd") || ends_with_ci(name, ".wtd") ||
           ends_with_ci(name, ".ydr") || ends_with_ci(name, ".yft") ||
           ends_with_ci(name, ".ydd");
}

std::vector<Entry> parse_entries(Reader &reader, const Archive &archive) {
    const auto header = reader.read(archive.base_offset, 16);
    if (rd32(header.data()) != RPF_MAGIC) throw std::runtime_error("invalid RPF7 magic");
    const uint32_t count = rd32(header.data() + 4);
    const uint32_t names_length = rd32(header.data() + 8);
    const uint32_t encryption = rd32(header.data() + 12);
    static const Crypto crypto;
    auto raw_entries = reader.read(archive.base_offset + 16, static_cast<size_t>(count) * 16);
    auto names = reader.read(archive.base_offset + 16 + static_cast<uint64_t>(count) * 16, names_length);
    raw_entries = crypto.decrypt(std::move(raw_entries), encryption, archive.name, static_cast<uint32_t>(archive.size));
    names = crypto.decrypt(std::move(names), encryption, archive.name, static_cast<uint32_t>(archive.size));
    std::vector<Entry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *blob = raw_entries.data() + static_cast<size_t>(i) * 16;
        const uint32_t first = rd32(blob);
        const uint32_t second = rd32(blob + 4);
        Entry &entry = entries[i];
        uint32_t name_offset = 0;
        if (second == 0x7FFFFF00U) {
            entry.type = EntryType::Directory;
            name_offset = first & 0xFFFFU;
            entry.entries_index = rd32(blob + 8);
            entry.entries_count = rd32(blob + 12);
        } else if ((second & 0x80000000U) == 0U) {
            const uint64_t low = rd64(blob);
            entry.type = EntryType::Binary;
            name_offset = static_cast<uint32_t>(low & 0xFFFFU);
            entry.file_size = static_cast<uint32_t>((low >> 16U) & 0xFFFFFFU);
            entry.file_offset = static_cast<uint32_t>((low >> 40U) & 0xFFFFFFU);
            entry.file_uncompressed_size = rd32(blob + 8);
        } else {
            entry.type = EntryType::Resource;
            name_offset = static_cast<uint32_t>(blob[0] | (blob[1] << 8U));
            entry.file_size = static_cast<uint32_t>(blob[2] | (blob[3] << 8U) | (blob[4] << 16U));
            entry.file_offset = static_cast<uint32_t>(blob[5] | (blob[6] << 8U) | (blob[7] << 16U)) & 0x7FFFFFU;
            entry.system_flags = rd32(blob + 8);
            entry.graphics_flags = rd32(blob + 12);
        }
        entry.name = read_name(names, name_offset);
    }
    if (entries.empty() || entries[0].type != EntryType::Directory)
        throw std::runtime_error("RPF root entry is not a directory");
    return entries;
}

uint32_t resource_stored_size(Reader &reader, const Archive &archive, const Entry &entry) {
    if (entry.file_size == 0)
        return resource_size_from_flags(entry.system_flags) + resource_size_from_flags(entry.graphics_flags);
    if (entry.file_size != 0xFFFFFFU) return entry.file_size;
    const auto header = reader.read(archive.base_offset + static_cast<uint64_t>(entry.file_offset) * RPF_BLOCK_SIZE, 16);
    return static_cast<uint32_t>(header[7]) | (static_cast<uint32_t>(header[14]) << 8U) |
           (static_cast<uint32_t>(header[5]) << 16U) | (static_cast<uint32_t>(header[2]) << 24U);
}

std::vector<uint8_t> standalone_resource(Reader &reader, const Archive &archive, const Entry &entry) {
    auto raw = reader.read(archive.base_offset + static_cast<uint64_t>(entry.file_offset) * RPF_BLOCK_SIZE,
                           resource_stored_size(reader, archive, entry));
    if (raw.size() >= 4 && rd32(raw.data()) == RSC7_MAGIC) return raw;
    std::vector<uint8_t> out(16 + (raw.size() > 16 ? raw.size() - 16 : 0));
    wr32(RSC7_MAGIC, out.data());
    wr32((((entry.system_flags >> 28U) & 0xFU) << 4U) | ((entry.graphics_flags >> 28U) & 0xFU), out.data() + 4);
    wr32(entry.system_flags, out.data() + 8);
    wr32(entry.graphics_flags, out.data() + 12);
    if (raw.size() > 16) std::copy(raw.begin() + 16, raw.end(), out.begin() + 16);
    return out;
}

int scan_archive(Reader &reader, const Archive &archive, RpfScanCallback callback, void *context) {
    const auto entries = parse_entries(reader, archive);
    int imported = 0;
    std::function<void(uint32_t, const std::wstring &)> walk;
    walk = [&](uint32_t dir_index, const std::wstring &prefix) {
        const Entry &dir = entries.at(dir_index);
        const uint32_t end = std::min<uint32_t>(dir.entries_index + dir.entries_count,
                                                static_cast<uint32_t>(entries.size()));
        for (uint32_t i = dir.entries_index; i < end; ++i) {
            const Entry &entry = entries[i];
            const std::wstring name = widen(entry.name);
            const std::wstring logical = prefix.empty() ? name : prefix + L"/" + name;
            if (entry.type == EntryType::Directory) {
                walk(i, logical);
            } else if (entry.type == EntryType::Binary && ends_with_ci(entry.name, ".rpf")) {
                const uint32_t nested_size = entry.file_size ? entry.file_size : entry.file_uncompressed_size;
                imported += scan_archive(reader, Archive{
                    archive.base_offset + static_cast<uint64_t>(entry.file_offset) * RPF_BLOCK_SIZE,
                    nested_size, logical, entry.name}, callback, context);
            } else if (entry.type != EntryType::Directory && is_supported(entry.name)) {
                const auto data = entry.type == EntryType::Resource
                    ? standalone_resource(reader, archive, entry)
                    : reader.read(archive.base_offset + static_cast<uint64_t>(entry.file_offset) * RPF_BLOCK_SIZE,
                                  entry.file_size ? entry.file_size : entry.file_uncompressed_size);
                if (callback(logical.c_str(), data.data(), data.size(), context)) ++imported;
            }
        }
    };
    walk(0, archive.prefix);
    return imported;
}

}  // namespace

extern "C" int rpf_scan_file(const wchar_t *path, RpfScanCallback callback, void *context,
                              char *error, size_t error_size) {
    try {
        Reader reader(path);
        const wchar_t *leaf = wcsrchr(path, L'\\');
        leaf = leaf ? leaf + 1 : path;
        const int required = WideCharToMultiByte(CP_UTF8, 0, leaf, -1, NULL, 0, NULL, NULL);
        std::string archive_name(required > 0 ? static_cast<size_t>(required) : 0, '\0');
        if (required > 0) {
            WideCharToMultiByte(CP_UTF8, 0, leaf, -1, &archive_name[0], required, NULL, NULL);
            archive_name.pop_back();
        }
        return scan_archive(reader, Archive{0, reader.size, {}, archive_name}, callback, context);
    } catch (const std::exception &ex) {
        if (error && error_size) {
            std::strncpy(error, ex.what(), error_size - 1);
            error[error_size - 1] = 0;
        }
        return -1;
    }
}
