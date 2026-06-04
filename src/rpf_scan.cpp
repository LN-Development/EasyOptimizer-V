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
        if (encryption == NG_ENCRYPTION) {
            if (ng_blob.size() < NG_KEYS_SIZE + 278528U || lut.size() != 256U)
                throw std::runtime_error("NG-encrypted RPF requires ng.dat and lut.dat beside the executable");
            return decrypt_ng(std::move(data), archive_name, archive_size);
        }
        /* Unknown/custom encryption marker (e.g. some modded RPFs such as the
         * "CFXP" 0x50584643 tag): these store a plain-text table of contents,
         * so pass it through unchanged rather than failing the whole archive. */
        return data;
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
        /* RoundA (shuffle=false) uses sequential byte indices (indexes[i]==i);
         * RoundB (shuffle=true) applies the GTA-NG byte permutation. The cell at
         * position 5 must be 5 for RoundA and 4 for RoundB (was hard-coded to 4,
         * which corrupted RoundA and broke NG decryption). */
        const size_t indexes[16] = {
            0U, shuffle ? 7U : 1U, shuffle ? 10U : 2U, shuffle ? 13U : 3U,
            shuffle ? 1U : 4U, shuffle ? 4U : 5U, shuffle ? 11U : 6U, shuffle ? 14U : 7U,
            shuffle ? 2U : 8U, shuffle ? 5U : 9U, shuffle ? 8U : 10U, shuffle ? 15U : 11U,
            shuffle ? 3U : 12U, shuffle ? 6U : 13U, shuffle ? 9U : 14U, shuffle ? 12U : 15U
        };
        uint32_t out[4];
        for (size_t row = 0; row < 4; ++row) {
            out[row] = rd32(ng_blob.data() + key_base + row * 4U);
            for (size_t col = 0; col < 4; ++col) {
                /* GTA-NG uses table[k][byte[k]]: the table cell index is the same
                 * permuted byte index, NOT a sequential cell index. */
                const size_t idx = indexes[row * 4U + col];
                out[row] ^= table(t + idx * 256U, block[idx]);
            }
        }
        for (size_t i = 0; i < 4; ++i) wr32(out[i], block + i * 4U);
    }

    /* AES-ECB encrypt the table of contents (mirror of decrypt_aes); used when
     * rewriting an AES-encrypted archive. Only the 16-byte-aligned prefix is
     * transformed, exactly like the decrypt path. */
    std::vector<uint8_t> encrypt_aes(std::vector<uint8_t> data) const {
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
            BCryptEncrypt(handle, data.data(), static_cast<ULONG>(aligned), NULL, NULL, 0,
                          data.data(), static_cast<ULONG>(aligned), &written, 0) < 0) {
            if (handle) BCryptDestroyKey(handle);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("failed to encrypt AES RPF table");
        }
        BCryptDestroyKey(handle);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return data;
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
    if (rd32(header.data()) != RPF_MAGIC) throw std::runtime_error("selected file is not a valid RPF7 archive");
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
                try {
                    imported += scan_archive(reader, Archive{
                        archive.base_offset + static_cast<uint64_t>(entry.file_offset) * RPF_BLOCK_SIZE,
                        nested_size, logical, entry.name}, callback, context);
                } catch (...) {
                    /* Some archives contain .rpf-named payloads that are not nested RPF7 containers. */
                }
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

/* ── RPF write-back (flat, non-NG) ─────────────────────────────────────── */

constexpr uint32_t RPF_RES_SIZE_MAX = 0xFFFFFFU;  /* 24-bit file_size field */

uint64_t align512(uint64_t v) { return (v + RPF_BLOCK_SIZE - 1) & ~(uint64_t)(RPF_BLOCK_SIZE - 1); }

int rewrite_archive(const wchar_t *src_path, const wchar_t *dst_path,
                    const RpfReplacement *reps, int rep_count) {
    Reader reader(src_path);

    const auto header = reader.read(0, 16);
    if (rd32(header.data()) != RPF_MAGIC)
        throw std::runtime_error("selected file is not a valid RPF7 archive");
    const uint32_t count        = rd32(header.data() + 4);
    const uint32_t names_length = rd32(header.data() + 8);
    const uint32_t encryption   = rd32(header.data() + 12);
    if (encryption == NG_ENCRYPTION)
        throw std::runtime_error("NG-encrypted RPF archives cannot be rewritten");

    static const Crypto crypto;
    const std::string self_name;  /* AES/plaintext don't need the archive name */
    auto entries = reader.read(16, static_cast<size_t>(count) * 16);
    auto names   = reader.read(16 + static_cast<uint64_t>(count) * 16, names_length);
    auto entries_plain = crypto.decrypt(entries, encryption, self_name, static_cast<uint32_t>(reader.size));
    auto names_plain   = crypto.decrypt(names, encryption, self_name, static_cast<uint32_t>(reader.size));

    /* Parse entries (mirror of parse_entries, kept local so we retain raw blobs). */
    Archive archive{0, reader.size, {}, self_name};
    std::vector<Entry> parsed(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *blob = entries_plain.data() + static_cast<size_t>(i) * 16;
        const uint32_t second = rd32(blob + 4);
        Entry &e = parsed[i];
        uint32_t name_offset = 0;
        if (second == 0x7FFFFF00U) {
            e.type = EntryType::Directory;
            name_offset = rd32(blob) & 0xFFFFU;
            e.entries_index = rd32(blob + 8);
            e.entries_count = rd32(blob + 12);
        } else if ((second & 0x80000000U) == 0U) {
            const uint64_t low = rd64(blob);
            e.type = EntryType::Binary;
            name_offset = static_cast<uint32_t>(low & 0xFFFFU);
            e.file_size = static_cast<uint32_t>((low >> 16U) & 0xFFFFFFU);
            e.file_offset = static_cast<uint32_t>((low >> 40U) & 0xFFFFFFU);
            e.file_uncompressed_size = rd32(blob + 8);
        } else {
            e.type = EntryType::Resource;
            name_offset = static_cast<uint32_t>(blob[0] | (blob[1] << 8U));
            e.file_size = static_cast<uint32_t>(blob[2] | (blob[3] << 8U) | (blob[4] << 16U));
            e.file_offset = static_cast<uint32_t>(blob[5] | (blob[6] << 8U) | (blob[7] << 16U)) & 0x7FFFFFU;
            e.system_flags = rd32(blob + 8);
            e.graphics_flags = rd32(blob + 12);
        }
        e.name = read_name(names_plain, name_offset);
    }
    if (parsed.empty() || parsed[0].type != EntryType::Directory)
        throw std::runtime_error("RPF root entry is not a directory");

    /* Resolve a logical path for every leaf file entry (no recursion into
     * nested .rpf containers; those stay opaque binaries). */
    std::vector<std::pair<uint32_t, std::wstring>> file_paths;  /* (index, logical) */
    std::function<void(uint32_t, const std::wstring &)> walk;
    walk = [&](uint32_t dir_index, const std::wstring &prefix) {
        const Entry &dir = parsed.at(dir_index);
        const uint32_t end = std::min<uint32_t>(dir.entries_index + dir.entries_count, count);
        for (uint32_t i = dir.entries_index; i < end; ++i) {
            const Entry &e = parsed[i];
            const std::wstring logical = prefix.empty() ? widen(e.name) : prefix + L"/" + widen(e.name);
            if (e.type == EntryType::Directory) walk(i, logical);
            else file_paths.push_back({i, logical});
        }
    };
    walk(0, L"");

    /* Layout-preserving rewrite: load the original image verbatim and leave every
     * untouched byte exactly where it is. Replaced entries are appended past the
     * end of the archive and only their table-of-contents record is patched, so
     * an edit never disturbs unrelated data (and a no-op save is bit-identical). */
    std::vector<uint8_t> image = reader.read(0, static_cast<size_t>(reader.size));
    uint64_t append_cursor = align512(image.size());

    std::vector<int> applied(rep_count, 0);
    for (auto &fp : file_paths) {
        const Entry &e = parsed[fp.first];
        int rep = -1;
        for (int r = 0; r < rep_count; ++r)
            if (reps[r].entry_path && _wcsicmp(reps[r].entry_path, fp.second.c_str()) == 0) { rep = r; break; }
        if (rep < 0) continue;                       /* untouched -> stays in place */

        const uint8_t *nd = reps[rep].data;
        const size_t   ns = reps[rep].data_size;
        const bool is_resource = (e.type == EntryType::Resource);
        applied[rep] = 1;

        uint32_t new_size = 0, sys = 0, gfx = 0, unc = 0;
        if (is_resource) {
            if (ns < 16 || rd32(nd) != RSC7_MAGIC)
                throw std::runtime_error("replacement for a resource entry is not an RSC7 file");
            if (ns >= RPF_RES_SIZE_MAX)
                throw std::runtime_error("replacement resource is too large for the RPF size field");
            new_size = static_cast<uint32_t>(ns);    /* full RSC7 file incl. header */
            sys = rd32(nd + 8);
            gfx = rd32(nd + 12);
        } else {
            if (ns >= RPF_RES_SIZE_MAX)
                throw std::runtime_error("replacement binary file is too large for the RPF size field");
            new_size = static_cast<uint32_t>(ns);
            unc = static_cast<uint32_t>(ns);
        }

        const uint64_t off = append_cursor;
        const uint64_t foff_blocks = off / RPF_BLOCK_SIZE;
        const uint64_t foff_limit = is_resource ? 0x7FFFFFULL : 0xFFFFFFULL;
        if (foff_blocks > foff_limit)
            throw std::runtime_error("archive grew beyond the RPF offset limit");

        image.resize(static_cast<size_t>(off + align512(ns)), 0);
        if (ns) std::copy(nd, nd + ns, image.begin() + static_cast<size_t>(off));
        append_cursor = off + align512(ns);

        /* Patch this entry's TOC record (offset/size/flags) in the decrypted blob. */
        uint8_t *blob = entries_plain.data() + static_cast<size_t>(fp.first) * 16;
        if (is_resource) {
            blob[2] = static_cast<uint8_t>(new_size);
            blob[3] = static_cast<uint8_t>(new_size >> 8U);
            blob[4] = static_cast<uint8_t>(new_size >> 16U);
            blob[5] = static_cast<uint8_t>(foff_blocks);
            blob[6] = static_cast<uint8_t>(foff_blocks >> 8U);
            blob[7] = static_cast<uint8_t>(((foff_blocks >> 16U) & 0x7FU) | 0x80U);
            wr32(sys, blob + 8);
            wr32(gfx, blob + 12);
        } else {
            const uint32_t name_offset = static_cast<uint32_t>(rd64(blob) & 0xFFFFU);
            const uint64_t low = (static_cast<uint64_t>(name_offset) & 0xFFFFU) |
                                 ((static_cast<uint64_t>(new_size) & 0xFFFFFFU) << 16U) |
                                 ((foff_blocks & 0xFFFFFFU) << 40U);
            for (int b = 0; b < 8; ++b) blob[b] = static_cast<uint8_t>(low >> (8U * b));
            wr32(unc, blob + 8);
        }
    }

    int applied_count = 0;
    for (int r = 0; r < rep_count; ++r) applied_count += applied[r];

    /* No replacements -> emit the original bytes untouched (bit-identical). */
    if (applied_count > 0) {
        std::vector<uint8_t> out_entries = entries_plain;
        if (encryption == AES_ENCRYPTION)
            out_entries = crypto.encrypt_aes(std::move(out_entries));
        std::copy(out_entries.begin(), out_entries.end(), image.begin() + 16);
    }

    std::ofstream out(dst_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to open destination RPF for writing");
    out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) throw std::runtime_error("failed to write destination RPF");
    return applied_count;
}

}  // namespace

extern "C" int rpf_scan_file(const wchar_t *path, RpfScanCallback callback, void *context,
                              char *error, size_t error_size) {
    try {
        Reader reader(path);
        /* The NG key index is derived from the archive's *file name* only, so
         * strip any directory prefix using either separator. */
        const wchar_t *bslash = wcsrchr(path, L'\\');
        const wchar_t *fslash = wcsrchr(path, L'/');
        const wchar_t *leaf = bslash > fslash ? bslash : fslash;
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

extern "C" int rpf_rewrite_file(const wchar_t *src_path, const wchar_t *dst_path,
                                const RpfReplacement *replacements, int replacement_count,
                                char *error, size_t error_size) {
    try {
        return rewrite_archive(src_path, dst_path, replacements, replacement_count);
    } catch (const std::exception &ex) {
        if (error && error_size) {
            std::strncpy(error, ex.what(), error_size - 1);
            error[error_size - 1] = 0;
        }
        return -1;
    }
}
