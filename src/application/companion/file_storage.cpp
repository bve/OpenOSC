#include "openosc/companion/file_storage.hpp"
#if defined(__arm__)
#include "openosc/storage/open_readers.hpp"
#include "lvgl.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <strings.h>

namespace openosc::companion {
namespace {
using files::Operation;
using files::Status;
constexpr char Root[] = "/openosc";
constexpr char Stage[] = "/openosc/.WEB-UPLOAD.TMP";

Status status(FRESULT result) {
    switch (result) {
    case FR_OK: return Status::Ok;
    case FR_NO_FILE: case FR_NO_PATH: return Status::Missing;
    case FR_EXIST: return Status::Exists;
    case FR_INVALID_NAME: return Status::Invalid;
    case FR_DENIED: case FR_WRITE_PROTECTED: return Status::Denied;
    case FR_LOCKED: case FR_TIMEOUT: return Status::Busy;
    default: return Status::Io;
    }
}
bool temporary(const char *name) {
    const auto length = std::strlen(name);
    return length >= 4 && !strcasecmp(name + length - 4, ".TMP");
}
bool validPath(const char *path, bool rootAllowed) {
    constexpr unsigned length = sizeof Root - 1;
    if (std::strncmp(path, Root, length) || (path[length] != 0 && path[length] != '/')) return false;
    if (!path[length]) return rootAllowed;
    for (const char *p = path + length + 1; *p;) {
        const auto *end = std::strchr(p, '/');
        if (!end) end = p + std::strlen(p);
        if (end == p || size_t(end - p) > FF_MAX_LFN || *p == ' ' ||
            end[-1] == ' ' || end[-1] == '.') return false;
        for (const char *ch = p; ch < end; ++ch)
            if (uint8_t(*ch) < ' ' || std::strchr("\\:*?\"<>|", *ch)) return false;
        if (!*end) return !temporary(p);
        p = end + 1;
    }
    return false;
}
}
namespace {
FileStorage *workspace = nullptr;
bool ready = false, mayWrite = false;
}
void releaseFileStorage() {
    if (!workspace) return;
    workspace->close();
    workspace->~FileStorage();
    lv_free(workspace);
    workspace = nullptr;
}
void serviceFileStorage(uint32_t now, bool mounted, bool mayModify) {
    ready = mounted;
    mayWrite = mayModify;
    if (workspace && (!ready || workspace->expired(now))) releaseFileStorage();
}
void answerFileStorage(const Frame &request, Frame &reply, uint32_t now) {
    reply = {};
    if (request.size < 3 || !request.data[1]) return;
    // Storage lives in the GUI heap only for an active request or transfer.
    if (ready && !workspace && request.data[2] == uint8_t(Operation::Path)) {
        if (void *memory = lv_malloc(sizeof(FileStorage))) workspace = new (memory) FileStorage;
    }
    if (workspace && ready) {
        workspace->admission(mayWrite);
        workspace->answer(request, reply, now);
        if (request.data[2] == uint8_t(Operation::Close)) releaseFileStorage();
    } else {
        reply.size = 2;
        reply.data[0] = request.data[1];
        reply.data[1] = uint8_t(request.data[2] == uint8_t(Operation::Close) ? Status::Ok : Status::Busy);
    }
}
void FileStorage::close() {
    if (openDirectory_) f_closedir(&directory_);
    if (download_ || upload_) f_close(&file_);
    if (download_) {
        --storage::openReaders;
        --storage::screenshotReaders;
        --storage::captureReaders;
    }
    if (staged_) f_unlink(Stage);
    download_ = upload_ = openDirectory_ = staged_ = false;
    pathSize_[0] = pathSize_[1] = 0;
    entry_.fname[0] = 0;
}
void FileStorage::answer(const Frame &request, Frame &reply, uint32_t now) {
    // The first byte is the outer link result. The next byte identifies the one queued request.
    reply = {};
    if (request.size < 3 || !request.data[1]) return;
    reply.size = 2;
    reply.data[0] = request.data[1];
    activity_ = now;
    const auto operation = Operation(request.data[2]);
    reply.data[1] = uint8_t(perform(operation, request.data + 3, request.size - 3, reply));
}
FileStorage::Status FileStorage::readName(unsigned offset, Frame &reply) const {
    const size_t length = std::strlen(entry_.fname);
    if (offset >= length) return Status::Invalid;
    const unsigned take = std::min<unsigned>(Payload - reply.size, length - offset);
    std::memcpy(reply.data + reply.size, entry_.fname + offset, take);
    reply.size += take;
    return Status::Ok;
}
FileStorage::Status FileStorage::next(Frame &reply) {
    if (!openDirectory_) return Status::Invalid;
    unsigned skipped = 0;
    do {
        const auto result = f_readdir(&directory_, &entry_);
        if (result != FR_OK) return status(result);
        if (!*entry_.fname) return Status::End;
        if (!temporary(entry_.fname)) {
            reply.data[2] = (entry_.fattrib & AM_DIR) != 0;
            put32(reply.data + 3, uint32_t(entry_.fsize));
            reply.data[7] = std::strlen(entry_.fname);
            reply.size = 8;
            return readName(0, reply);
        }
    } while (++skipped < 16);
    return Status::Skip; // The next request resumes the directory walk.
}
FileStorage::Status FileStorage::beginUpload(uint32_t bytes) {
    if (!mayModify_ || upload_ || download_ || openDirectory_) return Status::Busy;
    FILINFO info;
    const auto found = f_stat(path_[0], &info);
    if (found == FR_OK) return Status::Exists;
    if (found != FR_NO_FILE) return status(found);
    // Only this service creates this hidden staging file. Reclaim it after a power failure.
    const auto removed = f_unlink(Stage);
    if (removed != FR_OK && removed != FR_NO_FILE) return status(removed);
    const auto result = f_open(&file_, Stage, FA_CREATE_NEW | FA_WRITE);
    if (result != FR_OK) return status(result);
    staged_ = upload_ = true;
    left_ = bytes;
    return Status::Ok;
}
FileStorage::Status FileStorage::finishUpload() {
    if (!upload_ || left_) return Status::Invalid;
    if (!mayModify_) return Status::Busy;
    const auto closed = f_close(&file_);
    upload_ = false;
    if (closed != FR_OK) return status(closed);
    const auto result = f_rename(Stage, path_[0]);
    if (result == FR_OK) staged_ = false;
    return status(result);
}
FileStorage::Status FileStorage::perform(Operation op, const uint8_t *data, unsigned size, Frame &reply) {
    if (op == Operation::Close) return Status::Ok;
    if (op == Operation::Path) {
        if (size < 2 || (data[0] & 0x7e) || upload_ || download_ || openDirectory_) return Status::Invalid;
        const unsigned slot = data[0] & 1u;
        unsigned &offset = pathSize_[slot];
        if (data[0] & 128) offset = 0;
        if (offset + size > sizeof path_[slot] || std::memchr(data + 1, 0, size - 1)) return Status::Invalid;
        std::memcpy(path_[slot] + offset, data + 1, size - 1);
        offset += size - 1;
        path_[slot][offset] = 0;
        return Status::Ok;
    }
    if (!pathSize_[0] || !validPath(path_[0], op == Operation::List || op == Operation::Next || op == Operation::Name))
        return Status::Invalid;
    switch (op) {
    case Operation::List:
        if (size || openDirectory_ || upload_ || download_) return Status::Invalid;
        if (const auto result = f_opendir(&directory_, path_[0]); result != FR_OK) return status(result);
        openDirectory_ = true;
        return Status::Ok;
    case Operation::Next: return size ? Status::Invalid : next(reply);
    case Operation::Name: return size == 1 && openDirectory_ ? readName(data[0], reply) : Status::Invalid;
    case Operation::Download:
        if (size || download_ || upload_ || openDirectory_ || !mayModify_) return Status::Busy;
        if (const auto result = f_open(&file_, path_[0], FA_READ); result != FR_OK) return status(result);
        download_ = true;
        ++storage::openReaders;
        ++storage::screenshotReaders;
        ++storage::captureReaders;
        put32(reply.data + 2, uint32_t(f_size(&file_)));
        reply.size = 6;
        return Status::Ok;
    case Operation::Read: {
        if (size || !download_) return Status::Invalid;
        UINT got;
        const auto result = f_read(&file_, reply.data + 2, Payload - 2, &got);
        if (result == FR_OK) reply.size += got;
        return status(result);
    }
    case Operation::Upload: return size == 4 ? beginUpload(get32(data)) : Status::Invalid;
    case Operation::Write: {
        if (!upload_ || !size || size > left_) return Status::Invalid;
        UINT done;
        const auto result = f_write(&file_, data, size, &done);
        if (result != FR_OK) return status(result);
        if (done != size) return Status::Full;
        left_ -= done;
        return Status::Ok;
    }
    case Operation::Commit: return size ? Status::Invalid : finishUpload();
    case Operation::MakeDirectory: case Operation::Remove: case Operation::Rename:
        if (size || openDirectory_ || download_ || upload_) return Status::Invalid;
        if (!mayModify_ || storage::openReaders) return Status::Busy;
        if (op == Operation::MakeDirectory) return status(f_mkdir(path_[0]));
        if (op == Operation::Remove) return status(f_unlink(path_[0]));
        return pathSize_[1] && validPath(path_[1], false) ? status(f_rename(path_[0], path_[1])) : Status::Invalid;
    default: return Status::Invalid;
    }
}
}
#endif
