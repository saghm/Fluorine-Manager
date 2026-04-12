#ifdef BSARCH_DLL_EXPORT
#include "libbsarch.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>

#ifndef _WIN32
#include <bsatk/bsatk.h>

#include <memory>
#include <unordered_map>
#include <vector>
#endif

namespace libbsarch {

#ifndef _WIN32

// ---------------------------------------------------------------------------
// Linux implementation backed by bsatk.
//
// libbsarch is a Delphi-based archive library on Windows. Upstream MO2 uses
// a prebuilt DLL from vcpkg. On Linux we have no such runtime, so previously
// this file was a set of no-op stubs — which meant anything that went through
// the C++ wrappers (bs_archive::load_from_disk / extract_to_memory, used e.g.
// by the data-tab preview pipeline in OrganizerCore) silently returned empty.
//
// This replacement wires the read path through BSA::Archive from libs/bsatk,
// which is a pure C++ BSA reader already built and linked in MO2. The write
// side (creating archives, adding files, saving) is still stubbed; bsapacker
// linked against that stub before and keeps linking against it now.
// ---------------------------------------------------------------------------

struct LinuxArchive
{
    BSA::Archive archive;
    std::string filename;
    bool loaded = false;

    // Keep shared_ptrs alive so we can hand out a raw File* as an opaque
    // record id and safely dereference it later from *_by_record calls.
    std::unordered_map<BSA::File*, BSA::File::Ptr> fileRecords;
};

static LinuxArchive* as_archive(bsa_archive_t handle)
{
    return static_cast<LinuxArchive*>(handle);
}

static std::string wchar_to_utf8(const wchar_t* ws)
{
    if (!ws) return {};
    std::mbstate_t state{};
    const wchar_t* src = ws;
    std::size_t len    = std::wcsrtombs(nullptr, &src, 0, &state);
    if (len == static_cast<std::size_t>(-1)) {
        // Fallback: BSA paths are ASCII, just truncate each wchar to its low
        // byte. Good enough for any real Bethesda archive path.
        std::string out;
        for (const wchar_t* p = ws; *p; ++p) {
            out.push_back(static_cast<char>(*p & 0x7F));
        }
        return out;
    }
    std::string out(len, '\0');
    state = {};
    src   = ws;
    std::wcsrtombs(out.data(), &src, len + 1, &state);
    return out;
}

static void copy_to_wbuffer(const std::string& src, uint32_t buf_size, wchar_t* buf)
{
    if (!buf || buf_size == 0) return;
    std::mbstate_t state{};
    const char* p = src.c_str();
    std::size_t len = std::mbsrtowcs(buf, &p, buf_size - 1, &state);
    if (len == static_cast<std::size_t>(-1)) {
        // ASCII fallback
        std::size_t i = 0;
        for (; i + 1 < buf_size && i < src.size(); ++i) {
            buf[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
        }
        buf[i] = 0;
        return;
    }
    const std::size_t last = std::min<std::size_t>(len, buf_size - 1);
    buf[last] = 0;
}

static bsa_result_message_t make_message(bsa_result_code_t code)
{
    bsa_result_message_t r{};
    r.code = static_cast<int8_t>(code);
    return r;
}

static bsa_result_message_buffer_t make_extract_result(
    const std::vector<unsigned char>& data)
{
    bsa_result_message_buffer_t out{};
    out.message.code = BSA_RESULT_NONE;
    out.buffer.size  = static_cast<uint32_t>(data.size());
    if (!data.empty()) {
        auto* copy = new unsigned char[data.size()];
        std::memcpy(copy, data.data(), data.size());
        out.buffer.data = copy;
    } else {
        out.buffer.data = nullptr;
    }
    return out;
}

static bsa_result_message_buffer_t make_extract_error()
{
    bsa_result_message_buffer_t out{};
    out.message.code = BSA_RESULT_EXCEPTION;
    out.buffer.size  = 0;
    out.buffer.data  = nullptr;
    return out;
}

// --- Lifecycle ---

BSARCH_DLL_API(bsa_archive_t) bsa_create()
{
    return static_cast<bsa_archive_t>(new LinuxArchive());
}

BSARCH_DLL_API(bsa_result_message_t) bsa_free(bsa_archive_t handle)
{
    delete as_archive(handle);
    return make_message(BSA_RESULT_NONE);
}

BSARCH_DLL_API(bsa_result_message_t) bsa_close(bsa_archive_t handle)
{
    if (auto* wrapper = as_archive(handle)) {
        wrapper->archive.close();
        wrapper->fileRecords.clear();
        wrapper->loaded = false;
    }
    return make_message(BSA_RESULT_NONE);
}

// --- Reading ---

BSARCH_DLL_API(bsa_result_message_t)
bsa_load_from_file(bsa_archive_t handle, const wchar_t* file_path)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !file_path) {
        return make_message(BSA_RESULT_EXCEPTION);
    }
    wrapper->filename = wchar_to_utf8(file_path);
    const BSA::EErrorCode rc =
        wrapper->archive.read(wrapper->filename.c_str(), false);
    if (rc != BSA::ERROR_NONE && rc != BSA::ERROR_INVALIDHASHES) {
        return make_message(BSA_RESULT_EXCEPTION);
    }
    wrapper->loaded = true;
    return make_message(BSA_RESULT_NONE);
}

BSARCH_DLL_API(bsa_file_record_t)
bsa_find_file_record(bsa_archive_t handle, const wchar_t* file_path)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded || !file_path) return nullptr;

    BSA::File::Ptr file = wrapper->archive.findFile(wchar_to_utf8(file_path));
    if (!file) return nullptr;

    BSA::File* raw            = file.get();
    wrapper->fileRecords[raw] = file;
    return static_cast<bsa_file_record_t>(raw);
}

BSARCH_DLL_API(bsa_result_message_buffer_t)
bsa_extract_file_data_by_filename(bsa_archive_t handle, const wchar_t* file_path)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded || !file_path) {
        return make_extract_error();
    }
    BSA::File::Ptr file = wrapper->archive.findFile(wchar_to_utf8(file_path));
    if (!file) {
        return make_extract_error();
    }
    std::vector<unsigned char> data;
    if (wrapper->archive.extractToMemory(file, data) != BSA::ERROR_NONE) {
        return make_extract_error();
    }
    return make_extract_result(data);
}

BSARCH_DLL_API(bsa_result_message_buffer_t)
bsa_extract_file_data_by_record(bsa_archive_t handle, bsa_file_record_t record)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded || !record) {
        return make_extract_error();
    }
    auto it = wrapper->fileRecords.find(static_cast<BSA::File*>(record));
    if (it == wrapper->fileRecords.end()) {
        return make_extract_error();
    }
    std::vector<unsigned char> data;
    if (wrapper->archive.extractToMemory(it->second, data) != BSA::ERROR_NONE) {
        return make_extract_error();
    }
    return make_extract_result(data);
}

BSARCH_DLL_API(bsa_result_message_t)
bsa_file_data_free(bsa_archive_t, bsa_result_buffer_t buffer)
{
    delete[] static_cast<unsigned char*>(buffer.data);
    return make_message(BSA_RESULT_NONE);
}

BSARCH_DLL_API(bool)
bsa_file_exists(bsa_archive_t handle, const wchar_t* file_path)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded || !file_path) return false;
    return wrapper->archive.findFile(wchar_to_utf8(file_path)) != nullptr;
}

// --- Metadata ---

BSARCH_DLL_API(uint32_t)
bsa_filename_get(bsa_archive_t handle, uint32_t buf_size, wchar_t* buf)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper) return 0;
    copy_to_wbuffer(wrapper->filename, buf_size, buf);
    return static_cast<uint32_t>(wrapper->filename.size());
}

BSARCH_DLL_API(bsa_archive_type_t)
bsa_archive_type_get(bsa_archive_t handle)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded) return baNone;
    switch (wrapper->archive.getType()) {
    case TYPE_MORROWIND:
        return baTES3;
    case TYPE_OBLIVION:
        return baTES4;
    case TYPE_FALLOUT3:
        return baFO3;
    case TYPE_SKYRIMSE:
        return baSSE;
    case TYPE_FALLOUT4:
    case TYPE_FALLOUT4NG_7:
    case TYPE_FALLOUT4NG_8:
        return baFO4;
    case TYPE_STARFIELD:
        return baSF;
    case TYPE_STARFIELD_LZ4_TEXTURE:
        return baSFdds;
    default:
        return baNone;
    }
}

BSARCH_DLL_API(uint32_t) bsa_version_get(bsa_archive_t handle)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded) return 0;
    return static_cast<uint32_t>(wrapper->archive.getType());
}

BSARCH_DLL_API(uint32_t)
bsa_format_name_get(bsa_archive_t handle, uint32_t buf_size, wchar_t* buf)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper) return 0;
    const char* name = "BSA";
    if (wrapper->loaded) {
        switch (wrapper->archive.getType()) {
        case TYPE_MORROWIND:
            name = "Morrowind";
            break;
        case TYPE_OBLIVION:
            name = "Oblivion";
            break;
        case TYPE_FALLOUT3:
            name = "Fallout 3";
            break;
        case TYPE_SKYRIMSE:
            name = "Skyrim Special Edition";
            break;
        case TYPE_FALLOUT4:
        case TYPE_FALLOUT4NG_7:
        case TYPE_FALLOUT4NG_8:
            name = "Fallout 4";
            break;
        case TYPE_STARFIELD:
            name = "Starfield";
            break;
        case TYPE_STARFIELD_LZ4_TEXTURE:
            name = "Starfield DDS";
            break;
        default:
            break;
        }
    }
    const std::string s(name);
    copy_to_wbuffer(s, buf_size, buf);
    return static_cast<uint32_t>(s.size());
}

BSARCH_DLL_API(uint32_t) bsa_file_count_get(bsa_archive_t handle)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded) return 0;
    BSA::Folder::Ptr root = wrapper->archive.getRoot();
    return root ? root->countFiles() : 0;
}

BSARCH_DLL_API(uint32_t) bsa_archive_flags_get(bsa_archive_t handle)
{
    auto* wrapper = as_archive(handle);
    if (!wrapper || !wrapper->loaded) return 0;
    return wrapper->archive.getFlags();
}

// --- Write-side + misc no-ops (bsapacker links these; writes are not
//     implemented on Linux yet, same as before this refactor). ---

BSARCH_DLL_API(bsa_entry_list_t) bsa_entry_list_create()
{
    return nullptr;
}
BSARCH_DLL_API(bsa_result_message_t) bsa_entry_list_free(bsa_entry_list_t)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(uint32_t) bsa_entry_list_count(bsa_entry_list_t)
{
    return 0;
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_entry_list_add(bsa_entry_list_t, const wchar_t*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(uint32_t)
bsa_entry_list_get(bsa_entry_list_t, uint32_t, uint32_t, wchar_t*)
{
    return 0;
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_create_archive(bsa_archive_t, const wchar_t*, bsa_archive_type_t,
                   bsa_entry_list_t)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t) bsa_save(bsa_archive_t)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_disk(bsa_archive_t, const wchar_t*, const wchar_t*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_disk_root(bsa_archive_t, const wchar_t*, const wchar_t*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_memory(bsa_archive_t, const wchar_t*, uint32_t, bsa_buffer_t)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_extract_file(bsa_archive_t, const wchar_t*, const wchar_t*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_iterate_files(bsa_archive_t, bsa_file_iteration_proc_t, void*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(bsa_result_message_t)
bsa_get_resource_list(bsa_archive_t, bsa_entry_list_t, const wchar_t*)
{
    return make_message(BSA_RESULT_NONE);
}
BSARCH_DLL_API(void) bsa_archive_flags_set(bsa_archive_t, uint32_t) {}
BSARCH_DLL_API(uint32_t) bsa_file_flags_get(bsa_archive_t)
{
    return 0;
}
BSARCH_DLL_API(void) bsa_file_flags_set(bsa_archive_t, uint32_t) {}
BSARCH_DLL_API(bool) bsa_compress_get(bsa_archive_t)
{
    return false;
}
BSARCH_DLL_API(void) bsa_compress_set(bsa_archive_t, bool) {}
BSARCH_DLL_API(bool) bsa_share_data_get(bsa_archive_t)
{
    return false;
}
BSARCH_DLL_API(void) bsa_share_data_set(bsa_archive_t, bool) {}
BSARCH_DLL_API(void)
bsa_file_dds_info_callback_set(bsa_archive_t, bsa_file_dds_info_proc_t, void*)
{
}

#else  // _WIN32

// Windows path: upstream MO2 uses a prebuilt libbsarch DLL from vcpkg; this
// local tree is a compatibility stub that matches the exported symbol names.
// Keep it byte-compatible with previous behavior.

BSARCH_DLL_API(bsa_entry_list_t) bsa_entry_list_create() { return nullptr; }
BSARCH_DLL_API(bsa_result_message_t) bsa_entry_list_free(bsa_entry_list_t) { return {0}; }
BSARCH_DLL_API(uint32_t) bsa_entry_list_count(bsa_entry_list_t) { return 0; }
BSARCH_DLL_API(bsa_result_message_t) bsa_entry_list_add(bsa_entry_list_t, const wchar_t*) { return {0}; }
BSARCH_DLL_API(uint32_t) bsa_entry_list_get(bsa_entry_list_t, uint32_t, uint32_t, wchar_t*) { return 0; }
BSARCH_DLL_API(bsa_archive_t) bsa_create() { return nullptr; }
BSARCH_DLL_API(bsa_result_message_t) bsa_free(bsa_archive_t) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_load_from_file(bsa_archive_t, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_create_archive(bsa_archive_t, const wchar_t*, bsa_archive_type_t, bsa_entry_list_t) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_save(bsa_archive_t) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_add_file_from_disk(bsa_archive_t, const wchar_t*, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_add_file_from_disk_root(bsa_archive_t, const wchar_t*, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_add_file_from_memory(bsa_archive_t, const wchar_t*, uint32_t, bsa_buffer_t) { return {0}; }
BSARCH_DLL_API(bsa_file_record_t) bsa_find_file_record(bsa_archive_t, const wchar_t*) { return nullptr; }
BSARCH_DLL_API(bsa_result_message_buffer_t) bsa_extract_file_data_by_record(bsa_archive_t, bsa_file_record_t) { return {0}; }
BSARCH_DLL_API(bsa_result_message_buffer_t) bsa_extract_file_data_by_filename(bsa_archive_t, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_file_data_free(bsa_archive_t, bsa_result_buffer_t) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_extract_file(bsa_archive_t, const wchar_t*, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_iterate_files(bsa_archive_t, bsa_file_iteration_proc_t, void*) { return {0}; }
BSARCH_DLL_API(bool) bsa_file_exists(bsa_archive_t, const wchar_t*) { return false; }
BSARCH_DLL_API(bsa_result_message_t) bsa_get_resource_list(bsa_archive_t, bsa_entry_list_t, const wchar_t*) { return {0}; }
BSARCH_DLL_API(bsa_result_message_t) bsa_close(bsa_archive_t) { return {0}; }
BSARCH_DLL_API(uint32_t) bsa_filename_get(bsa_archive_t, uint32_t, wchar_t*) { return 0; }
BSARCH_DLL_API(bsa_archive_type_t) bsa_archive_type_get(bsa_archive_t) { return bsa_archive_type_t::baNone; }
BSARCH_DLL_API(uint32_t) bsa_version_get(bsa_archive_t) { return 0; }
BSARCH_DLL_API(uint32_t) bsa_format_name_get(bsa_archive_t, uint32_t, wchar_t*) { return 0; }
BSARCH_DLL_API(uint32_t) bsa_file_count_get(bsa_archive_t) { return 0; }
BSARCH_DLL_API(uint32_t) bsa_archive_flags_get(bsa_archive_t) { return 0; }
BSARCH_DLL_API(void) bsa_archive_flags_set(bsa_archive_t, uint32_t) {}
BSARCH_DLL_API(uint32_t) bsa_file_flags_get(bsa_archive_t) { return 0; }
BSARCH_DLL_API(void) bsa_file_flags_set(bsa_archive_t, uint32_t) {}
BSARCH_DLL_API(bool) bsa_compress_get(bsa_archive_t) { return false; }
BSARCH_DLL_API(void) bsa_compress_set(bsa_archive_t, bool) {}
BSARCH_DLL_API(bool) bsa_share_data_get(bsa_archive_t) { return false; }
BSARCH_DLL_API(void) bsa_share_data_set(bsa_archive_t, bool) {}
BSARCH_DLL_API(void) bsa_file_dds_info_callback_set(bsa_archive_t, bsa_file_dds_info_proc_t, void*) {}

#endif  // !_WIN32

}  // namespace libbsarch
#endif  // BSARCH_DLL_EXPORT
