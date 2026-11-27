#include "file_portal.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openosc::companion {
namespace {
using files::Operation;
using files::Status;

bool receive(httpd_req_t *req, void *out, size_t size) {
    auto *bytes = static_cast<char *>(out);
    while (size) {
        const int got = httpd_req_recv(req, bytes, size);
        if (got <= 0) return false;
        size -= got;
        bytes += got;
    }
    return true;
}

class Session {
public:
    explicit Session(FileMailbox &mailbox) : mailbox_(mailbox) { mailbox_.active(true); }
    ~Session() {
        if (answered_) {
            Frame reply;
            mailbox_.call(Operation::Close, nullptr, 0, reply);
        }
        mailbox_.active(false);
    }
    Status call(Operation op, const uint8_t *bytes = nullptr, unsigned size = 0) {
        if (!mailbox_.call(op, bytes, size, reply_) || reply_.size < 2) return Status::Busy;
        answered_ = true;
        return Status(reply_.data[1]);
    }
    Status path(httpd_req_t *req, size_t &left, unsigned slot) {
        uint8_t header[2];
        if (left < sizeof header || !receive(req, header, sizeof header)) return Status::Invalid;
        const unsigned size = unsigned(header[0]) | unsigned(header[1]) << 8;
        if (!size || size >= files::PathBytes || left - sizeof header < size) return Status::Invalid;
        left -= sizeof header + size;
        unsigned processed = 0;
        while (processed < size) {
            uint8_t data[files::Chunk];
            const unsigned take = std::min<unsigned>(sizeof data - 1, size - processed);
            data[0] = slot | (processed ? 0 : 128);
            if (!receive(req, data + 1, take)) return Status::Invalid;
            const auto result = call(Operation::Path, data, take + 1);
            if (result != Status::Ok) return result;
            processed += take;
        }
        return Status::Ok;
    }
    const Frame &reply() const { return reply_; }
private:
    FileMailbox &mailbox_;
    Frame reply_{};
    bool answered_ = false;
};

esp_err_t error(httpd_req_t *req, Status result) {
    const char *code = "500 Internal Server Error";
    const char *message = "SPI flash operation failed.";
    switch (result) {
    case Status::Invalid: code = "400 Bad Request"; message = "Invalid file path or request."; break;
    case Status::Missing: code = "404 Not Found"; message = "The file or folder was not found."; break;
    case Status::Busy: code = "503 Service Unavailable"; message = "Instrument or storage busy. End the local file operation or pause acquisition and retry."; break;
    case Status::Exists: code = "409 Conflict"; message = "A file or folder already uses this name."; break;
    case Status::Denied: code = "409 Conflict"; message = "The folder must be empty, or the item is read-only."; break;
    case Status::Full: code = "507 Insufficient Storage"; message = "SPI flash is full."; break;
    default: break;
    }
    httpd_resp_set_status(req, code);
    return httpd_resp_sendstr(req, message);
}

esp_err_t list(httpd_req_t *req, Session &session) {
    const auto opened = session.call(Operation::List);
    if (opened != Status::Ok) return error(req, opened);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    if (httpd_resp_sendstr_chunk(req, "[") != ESP_OK) return ESP_FAIL;
    bool first = true;
    for (;;) {
        const auto result = session.call(Operation::Next);
        if (result == Status::End) break;
        if (result == Status::Skip) continue;
        const auto &reply = session.reply();
        if (result != Status::Ok || reply.size < 9) return ESP_FAIL;
        const bool directory = reply.data[2] != 0;
        const unsigned long size = get32(reply.data + 3);
        const unsigned length = reply.data[7];
        char name[256];
        unsigned have = reply.size - 8;
        if (have > length) return ESP_FAIL;
        std::memcpy(name, reply.data + 8, have);
        while (have < length) {
            const uint8_t offset = have;
            if (session.call(Operation::Name, &offset, 1) != Status::Ok || reply.size <= 2 ||
                unsigned(reply.size - 2) > length - have) return ESP_FAIL;
            std::memcpy(name + have, reply.data + 2, reply.size - 2);
            have += reply.size - 2;
        }
        // FatFs forbids ASCII control characters and quotes in names. Escape any unusual on-disk bytes.
        char row[2 * sizeof name + 64];
        size_t at = 0;
        if (!first) row[at++] = ',';
        row[at++] = '[';
        row[at++] = '"';
        for (unsigned i = 0; i < length; ++i) {
            const char ch = name[i];
            if (ch == '"' || ch == '\\') row[at++] = '\\';
            row[at++] = uint8_t(ch) < ' ' ? '?' : ch;
        }
        std::snprintf(row + at, sizeof row - at, "\",%s,%lu]", directory ? "true" : "false", size);
        if (httpd_resp_sendstr_chunk(req, row) != ESP_OK) return ESP_FAIL;
        first = false;
    }
    if (httpd_resp_sendstr_chunk(req, "]") != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t download(httpd_req_t *req, Session &session) {
    const auto opened = session.call(Operation::Download);
    if (opened != Status::Ok) return error(req, opened);
    if (session.reply().size != 6) return ESP_FAIL;
    uint32_t left = get32(session.reply().data + 2);
    httpd_resp_set_type(req, "application/octet-stream");
    while (left) {
        if (session.call(Operation::Read) != Status::Ok) return ESP_FAIL;
        const auto &reply = session.reply();
        const unsigned size = reply.size - 2;
        if (!size || size > left || httpd_resp_send_chunk(req, reinterpret_cast<const char *>(reply.data + 2), size) != ESP_OK)
            return ESP_FAIL;
        left -= size;
    }
    return httpd_resp_sendstr_chunk(req, nullptr);
}
Status upload(httpd_req_t *req, Session &session, size_t left) {
    uint8_t bytes[files::Chunk];
    put32(bytes, left);
    auto result = session.call(Operation::Upload, bytes, 4);
    if (result != Status::Ok) return result;
    while (left) {
        const unsigned take = std::min(sizeof bytes, left);
        if (!receive(req, bytes, take)) return Status::Io;
        result = session.call(Operation::Write, bytes, take);
        if (result != Status::Ok) return result;
        left -= take;
    }
    return session.call(Operation::Commit);
}
}

esp_err_t serveFiles(httpd_req_t *req, FileMailbox &mailbox) {
    char action[16];
    if (httpd_req_get_hdr_value_str(req, "X-OpenOSC-Operation", action, sizeof action) != ESP_OK)
        return error(req, Status::Invalid);
    Session session(mailbox);
    size_t left = req->content_len;
    if (const auto result = session.path(req, left, 0); result != Status::Ok) return error(req, result);
    if (!std::strcmp(action, "list") && !left) return list(req, session);
    if (!std::strcmp(action, "download") && !left) return download(req, session);
    Status result = Status::Invalid;
    if (!std::strcmp(action, "upload")) result = upload(req, session, left);
    else if (!std::strcmp(action, "rename")) {
        result = session.path(req, left, 1);
        if (result == Status::Ok) result = left ? Status::Invalid : session.call(Operation::Rename);
    } else if (!left && !std::strcmp(action, "mkdir")) result = session.call(Operation::MakeDirectory);
    else if (!left && !std::strcmp(action, "remove")) result = session.call(Operation::Remove);
    if (result != Status::Ok) return error(req, result);
    return httpd_resp_sendstr(req, "OK");
}
}
