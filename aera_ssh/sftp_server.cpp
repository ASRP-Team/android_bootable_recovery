/*
 * AERA recovery SFTP subsystem.
 *
 * Dropbear has no built-in sftp-server, so this standalone binary speaks the
 * SFTP v3 protocol (draft-ietf-secsh-filexfer-02) over stdin/stdout and is
 * spawned by Dropbear when a client requests the "sftp" subsystem.
 *
 * Recovery intentionally runs as root with the whole filesystem accessible
 * (same trust model as `adb shell`), so there is no chroot here.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

// ---------------------------------------------------------------------------
// SFTP v3 constants
// ---------------------------------------------------------------------------
enum : uint8_t {
    kFxpInit = 1,
    kFxpVersion = 2,
    kFxpOpen = 3,
    kFxpClose = 4,
    kFxpRead = 5,
    kFxpWrite = 6,
    kFxpLstat = 7,
    kFxpFstat = 8,
    kFxpSetstat = 9,
    kFxpFsetstat = 10,
    kFxpOpendir = 11,
    kFxpReaddir = 12,
    kFxpRemove = 13,
    kFxpMkdir = 14,
    kFxpRmdir = 15,
    kFxpRealpath = 16,
    kFxpStat = 17,
    kFxpRename = 18,
    kFxpReadlink = 19,
    kFxpSymlink = 20,
    kFxpStatus = 101,
    kFxpHandle = 102,
    kFxpData = 103,
    kFxpName = 104,
    kFxpAttrs = 105,
    kFxpExtended = 200,
    kFxpExtendedReply = 201,
};

enum : uint32_t {
    kAttrSize = 0x00000001,
    kAttrUidGid = 0x00000002,
    kAttrPermissions = 0x00000004,
    kAttrAcmodtime = 0x00000008,
    kAttrExtended = 0x80000000,
};

enum : uint32_t {
    kFxfRead = 0x00000001,
    kFxfWrite = 0x00000002,
    kFxfAppend = 0x00000004,
    kFxfCreat = 0x00000008,
    kFxfTrunc = 0x00000010,
    kFxfExcl = 0x00000020,
};

enum : uint32_t {
    kStatusOk = 0,
    kStatusEof = 1,
    kStatusNoSuchFile = 2,
    kStatusPermissionDenied = 3,
    kStatusFailure = 4,
    kStatusBadMessage = 5,
    kStatusOpUnsupported = 8,
};

constexpr size_t kMaxPacket = 1024 * 1024;
constexpr size_t kReaddirPayload = 4096;
constexpr off_t kMaxReadRequest = 256 * 1024;

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------
bool ReadFull(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
        } else if (n == 0) {
            return false;
        } else if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool WriteFull(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
        } else if (n < 0 && errno != EINTR) {
            return false;
        }
    }
    return true;
}

uint32_t GetU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint64_t GetU64(const uint8_t* p) {
    return (static_cast<uint64_t>(GetU32(p)) << 32) | GetU32(p + 4);
}

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data, size_t offset = 0)
        : data_(data), pos_(offset) {}

    bool U8(uint8_t* out) {
        if (pos_ + 1 > data_.size()) return false;
        *out = data_[pos_++];
        return true;
    }

    bool U32(uint32_t* out) {
        if (pos_ + 4 > data_.size()) return false;
        *out = GetU32(data_.data() + pos_);
        pos_ += 4;
        return true;
    }

    bool U64(uint64_t* out) {
        if (pos_ + 8 > data_.size()) return false;
        *out = GetU64(data_.data() + pos_);
        pos_ += 8;
        return true;
    }

    bool String(std::string* out) {
        uint32_t len = 0;
        if (!U32(&len)) return false;
        if (pos_ + len > data_.size()) return false;
        out->assign(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return true;
    }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_;
};

class Writer {
public:
    Writer() = default;

    void U8(uint8_t v) { buf_.push_back(v); }

    void U32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v >> 24));
        buf_.push_back(static_cast<uint8_t>(v >> 16));
        buf_.push_back(static_cast<uint8_t>(v >> 8));
        buf_.push_back(static_cast<uint8_t>(v));
    }

    void U64(uint64_t v) {
        U32(static_cast<uint32_t>(v >> 32));
        U32(static_cast<uint32_t>(v));
    }

    void Bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    void String(const std::string& s) {
        U32(static_cast<uint32_t>(s.size()));
        Bytes(s.data(), s.size());
    }

    size_t Size() const { return buf_.size(); }
    const std::vector<uint8_t>& Buffer() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

bool SendPacket(const Writer& writer) {
    uint32_t len = static_cast<uint32_t>(writer.Size());
    uint8_t header[4] = {
        static_cast<uint8_t>(len >> 24),
        static_cast<uint8_t>(len >> 16),
        static_cast<uint8_t>(len >> 8),
        static_cast<uint8_t>(len),
    };
    return WriteFull(STDOUT_FILENO, header, sizeof(header)) &&
           WriteFull(STDOUT_FILENO, writer.Buffer().data(), writer.Size());
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
struct Attributes {
    uint32_t flags = 0;
    uint64_t size = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t permissions = 0;
    uint32_t atime = 0;
    uint32_t mtime = 0;
};

Attributes FromStat(const struct stat& st) {
    Attributes a;
    a.flags = kAttrSize | kAttrUidGid | kAttrPermissions | kAttrAcmodtime;
    a.size = static_cast<uint64_t>(st.st_size);
    a.uid = static_cast<uint32_t>(st.st_uid);
    a.gid = static_cast<uint32_t>(st.st_gid);
    a.permissions = static_cast<uint32_t>(st.st_mode) & 07777u;
    a.atime = static_cast<uint32_t>(st.st_atime);
    a.mtime = static_cast<uint32_t>(st.st_mtime);
    return a;
}

void PutAttributes(Writer& w, const Attributes& a) {
    w.U32(a.flags);
    if (a.flags & kAttrSize) w.U64(a.size);
    if (a.flags & kAttrUidGid) {
        w.U32(a.uid);
        w.U32(a.gid);
    }
    if (a.flags & kAttrPermissions) w.U32(a.permissions);
    if (a.flags & kAttrAcmodtime) {
        w.U32(a.atime);
        w.U32(a.mtime);
    }
    if (a.flags & kAttrExtended) w.U32(0);
}

bool ReadAttributes(Reader& r, Attributes* a, uint32_t* flags) {
    if (!r.U32(flags)) return false;
    a->flags = *flags;
    if (*flags & kAttrSize) {
        if (!r.U64(&a->size)) return false;
    }
    if (*flags & kAttrUidGid) {
        if (!r.U32(&a->uid) || !r.U32(&a->gid)) return false;
    }
    if (*flags & kAttrPermissions) {
        if (!r.U32(&a->permissions)) return false;
    }
    if (*flags & kAttrAcmodtime) {
        if (!r.U32(&a->atime) || !r.U32(&a->mtime)) return false;
    }
    if (*flags & kAttrExtended) {
        uint32_t count = 0;
        if (!r.U32(&count)) return false;
        for (uint32_t i = 0; i < count; i++) {
            std::string type;
            std::string data;
            if (!r.String(&type) || !r.String(&data)) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Replies
// ---------------------------------------------------------------------------
void SendStatus(uint32_t id, uint32_t code, const char* message) {
    Writer w;
    w.U8(kFxpStatus);
    w.U32(id);
    w.U32(code);
    w.String(message != nullptr ? message : "");
    w.String("");  // language tag
    SendPacket(w);
}

void SendStatusFromErrno(uint32_t id) {
    uint32_t code = kStatusFailure;
    if (errno == ENOENT || errno == ENOTDIR) {
        code = kStatusNoSuchFile;
    } else if (errno == EACCES || errno == EPERM) {
        code = kStatusPermissionDenied;
    }
    SendStatus(id, code, strerror(errno));
}

void SendHandle(uint32_t id, uint32_t handle) {
    Writer w;
    w.U8(kFxpHandle);
    w.U32(id);
    w.U32(4);
    w.U32(handle);
    SendPacket(w);
}

void SendAttrs(uint32_t id, const Attributes& a) {
    Writer w;
    w.U8(kFxpAttrs);
    w.U32(id);
    PutAttributes(w, a);
    SendPacket(w);
}

void SendData(uint32_t id, const uint8_t* data, size_t len) {
    Writer w;
    w.U8(kFxpData);
    w.U32(id);
    w.U32(static_cast<uint32_t>(len));
    w.Bytes(data, len);
    SendPacket(w);
}

void SendName(uint32_t id, const std::vector<std::string>& names,
              const std::vector<Attributes>& attrs, bool longname) {
    Writer w;
    w.U8(kFxpName);
    w.U32(id);
    w.U32(static_cast<uint32_t>(names.size()));
    for (size_t i = 0; i < names.size(); i++) {
        const std::string& name = names[i];
        w.String(name);
        if (longname) {
            char mode[11];
            uint32_t perms = attrs[i].permissions;
            mode[0] = ((perms & 040000) != 0) ? 'd' : '-';
            static const char kRwx[] = "rwx";
            for (int b = 0; b < 3; b++) {
                for (int j = 0; j < 3; j++) {
                    mode[1 + b * 3 + j] =
                        (perms & (0400 >> (b * 3 + j))) ? kRwx[j] : '-';
                }
            }
            mode[10] = '\0';
            time_t mt = static_cast<time_t>(attrs[i].mtime);
            char when[32];
            struct tm tmv;
            localtime_r(&mt, &tmv);
            strftime(when, sizeof(when), "%b %e %H:%M", &tmv);
            char line[512];
            snprintf(line, sizeof(line), "%s %4u %-8u %-8u %10llu %s %s",
                     mode, 1u, attrs[i].uid, attrs[i].gid,
                     static_cast<unsigned long long>(attrs[i].size),
                     when, name.c_str());
            w.String(line);
        } else {
            w.String(name);
        }
        PutAttributes(w, attrs[i]);
    }
    SendPacket(w);
}

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
struct Handle {
    int fd = -1;
    DIR* dir = nullptr;
    bool dir_eof = false;

    ~Handle() {
        if (fd >= 0) close(fd);
        if (dir != nullptr) closedir(dir);
    }
};

std::unordered_map<uint32_t, Handle*> g_handles;
uint32_t g_next_handle = 1;

uint32_t Register(Handle* h) {
    uint32_t id = g_next_handle++;
    g_handles[id] = h;
    return id;
}

Handle* Lookup(uint32_t id) {
    auto it = g_handles.find(id);
    return it == g_handles.end() ? nullptr : it->second;
}

void Release(uint32_t id) {
    auto it = g_handles.find(id);
    if (it == g_handles.end()) return;
    delete it->second;
    g_handles.erase(it);
}

// ---------------------------------------------------------------------------
// Path handling
// ---------------------------------------------------------------------------
// Recovery exposes the whole filesystem; relative paths are anchored at "/".
std::string Resolve(const std::string& path) {
    std::string full = (!path.empty() && path[0] == '/') ? path : "/" + path;
    char resolved[PATH_MAX];
    if (realpath(full.c_str(), resolved) != nullptr) {
        return resolved;
    }
    return full;
}

// ---------------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------------
void HandleOpen(uint32_t id, Reader& r) {
    std::string path;
    uint32_t pflags = 0;
    Attributes attr;
    uint32_t attr_flags = 0;
    if (!r.String(&path) || !r.U32(&pflags) ||
        !ReadAttributes(r, &attr, &attr_flags)) {
        SendStatus(id, kStatusBadMessage, "bad open request");
        return;
    }

    int flags = 0;
    bool read = (pflags & kFxfRead) != 0;
    bool write = (pflags & kFxfWrite) != 0;
    if (read && write) {
        flags = O_RDWR;
    } else if (write) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }
    if (pflags & kFxfCreat) flags |= O_CREAT;
    if (pflags & kFxfTrunc) flags |= O_TRUNC;
    if (pflags & kFxfExcl) flags |= O_EXCL;
    if (pflags & kFxfAppend) flags |= O_APPEND;

    mode_t mode = 0600;
    if (attr_flags & kAttrPermissions) {
        mode = static_cast<mode_t>(attr.permissions & 07777);
    }

    std::string full = Resolve(path);
    int fd = open(full.c_str(), flags, mode);
    if (fd < 0) {
        SendStatusFromErrno(id);
        return;
    }
    if (attr_flags & kAttrSize) {
        if (ftruncate(fd, static_cast<off_t>(attr.size)) != 0) {
            int saved = errno;
            close(fd);
            errno = saved;
            SendStatusFromErrno(id);
            return;
        }
    }

    auto* h = new Handle();
    h->fd = fd;
    SendHandle(id, Register(h));
}

void HandleClose(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    if (!r.U32(&handle) || Lookup(handle) == nullptr) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    Release(handle);
    SendStatus(id, kStatusOk, "ok");
}

void HandleRead(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    uint64_t offset = 0;
    uint32_t len = 0;
    if (!r.U32(&handle) || !r.U64(&offset) || !r.U32(&len)) {
        SendStatus(id, kStatusBadMessage, "bad read request");
        return;
    }
    Handle* h = Lookup(handle);
    if (h == nullptr || h->fd < 0) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    if (len > kMaxReadRequest) len = kMaxReadRequest;

    std::vector<uint8_t> buf(len);
    ssize_t n = pread(h->fd, buf.data(), len, static_cast<off_t>(offset));
    if (n < 0) {
        SendStatusFromErrno(id);
        return;
    }
    if (n == 0) {
        SendStatus(id, kStatusEof, "eof");
        return;
    }
    SendData(id, buf.data(), static_cast<size_t>(n));
}

void HandleWrite(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    uint64_t offset = 0;
    std::string data;
    if (!r.U32(&handle) || !r.U64(&offset) || !r.String(&data)) {
        SendStatus(id, kStatusBadMessage, "bad write request");
        return;
    }
    Handle* h = Lookup(handle);
    if (h == nullptr || h->fd < 0) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    size_t done = 0;
    while (done < data.size()) {
        ssize_t n = pwrite(h->fd, data.data() + done, data.size() - done,
                           static_cast<off_t>(offset + done));
        if (n > 0) {
            done += static_cast<size_t>(n);
        } else if (n < 0 && errno != EINTR) {
            SendStatusFromErrno(id);
            return;
        }
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleStat(uint32_t id, Reader& r, bool follow) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad stat request");
        return;
    }
    struct stat st;
    std::string full = Resolve(path);
    int rc = follow ? stat(full.c_str(), &st) : lstat(full.c_str(), &st);
    if (rc != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendAttrs(id, FromStat(st));
}

void HandleFstat(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    if (!r.U32(&handle)) {
        SendStatus(id, kStatusBadMessage, "bad fstat request");
        return;
    }
    Handle* h = Lookup(handle);
    if (h == nullptr || h->fd < 0) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    struct stat st;
    if (fstat(h->fd, &st) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendAttrs(id, FromStat(st));
}

bool ApplySetstat(int fd, const std::string& path, const Attributes& a) {
    if (a.flags & kAttrSize) {
        if (fd >= 0) {
            if (ftruncate(fd, static_cast<off_t>(a.size)) != 0) return false;
        } else if (truncate(path.c_str(), static_cast<off_t>(a.size)) != 0) {
            return false;
        }
    }
    if (a.flags & kAttrUidGid) {
        if (fd >= 0) {
            if (fchown(fd, static_cast<uid_t>(a.uid),
                       static_cast<gid_t>(a.gid)) != 0) return false;
        } else if (chown(path.c_str(), static_cast<uid_t>(a.uid),
                         static_cast<gid_t>(a.gid)) != 0) {
            return false;
        }
    }
    if (a.flags & kAttrPermissions) {
        mode_t mode = static_cast<mode_t>(a.permissions & 07777);
        if (fd >= 0) {
            if (fchmod(fd, mode) != 0) return false;
        } else if (chmod(path.c_str(), mode) != 0) {
            return false;
        }
    }
    if (a.flags & kAttrAcmodtime) {
        struct timespec times[2];
        times[0].tv_sec = static_cast<time_t>(a.atime);
        times[0].tv_nsec = 0;
        times[1].tv_sec = static_cast<time_t>(a.mtime);
        times[1].tv_nsec = 0;
        if (fd >= 0) {
            if (futimens(fd, times) != 0) return false;
        } else if (utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
            return false;
        }
    }
    return true;
}

void HandleSetstat(uint32_t id, Reader& r) {
    std::string path;
    Attributes a;
    uint32_t flags = 0;
    if (!r.String(&path) || !ReadAttributes(r, &a, &flags)) {
        SendStatus(id, kStatusBadMessage, "bad setstat request");
        return;
    }
    if (!ApplySetstat(-1, Resolve(path), a)) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleFsetstat(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    Attributes a;
    uint32_t flags = 0;
    if (!r.U32(&handle) || !ReadAttributes(r, &a, &flags)) {
        SendStatus(id, kStatusBadMessage, "bad fsetstat request");
        return;
    }
    Handle* h = Lookup(handle);
    if (h == nullptr || h->fd < 0) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    if (!ApplySetstat(h->fd, "", a)) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleOpendir(uint32_t id, Reader& r) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad opendir request");
        return;
    }
    std::string full = Resolve(path);
    DIR* dir = opendir(full.c_str());
    if (dir == nullptr) {
        SendStatusFromErrno(id);
        return;
    }
    auto* h = new Handle();
    h->dir = dir;
    SendHandle(id, Register(h));
}

void HandleReaddir(uint32_t id, Reader& r) {
    uint32_t handle = 0;
    if (!r.U32(&handle)) {
        SendStatus(id, kStatusBadMessage, "bad readdir request");
        return;
    }
    Handle* h = Lookup(handle);
    if (h == nullptr || h->dir == nullptr) {
        SendStatus(id, kStatusFailure, "invalid handle");
        return;
    }
    if (h->dir_eof) {
        SendStatus(id, kStatusEof, "eof");
        return;
    }

    std::vector<std::string> names;
    std::vector<Attributes> attrs;
    size_t payload = 0;
    while (true) {
        errno = 0;
        struct dirent* entry = readdir(h->dir);
        if (entry == nullptr) {
            h->dir_eof = true;
            if (names.empty()) {
                if (errno != 0) {
                    SendStatusFromErrno(id);
                } else {
                    SendStatus(id, kStatusEof, "eof");
                }
                return;
            }
            break;
        }
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        // lstat for a per-entry longname; failures degrade to empty attrs.
        struct stat st;
        if (fstatat(dirfd(h->dir), name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        Attributes a = FromStat(st);
        payload += name.size() + 128;
        names.push_back(name);
        attrs.push_back(a);
        if (payload >= kReaddirPayload) break;
    }
    SendName(id, names, attrs, true);
}

void HandleRemove(uint32_t id, Reader& r) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad remove request");
        return;
    }
    std::string full = Resolve(path);
    struct stat st;
    int rc;
    if (lstat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        rc = rmdir(full.c_str());
    } else {
        rc = unlink(full.c_str());
    }
    if (rc != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleMkdir(uint32_t id, Reader& r) {
    std::string path;
    Attributes a;
    uint32_t flags = 0;
    if (!r.String(&path) || !ReadAttributes(r, &a, &flags)) {
        SendStatus(id, kStatusBadMessage, "bad mkdir request");
        return;
    }
    mode_t mode = 0755;
    if (flags & kAttrPermissions) mode = a.permissions & 07777;
    std::string full = Resolve(path);
    if (mkdir(full.c_str(), mode) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleRmdir(uint32_t id, Reader& r) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad rmdir request");
        return;
    }
    std::string full = Resolve(path);
    if (rmdir(full.c_str()) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleRename(uint32_t id, Reader& r) {
    std::string oldpath;
    std::string newpath;
    if (!r.String(&oldpath) || !r.String(&newpath)) {
        SendStatus(id, kStatusBadMessage, "bad rename request");
        return;
    }
    if (rename(Resolve(oldpath).c_str(), Resolve(newpath).c_str()) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleReadlink(uint32_t id, Reader& r) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad readlink request");
        return;
    }
    std::string full = Resolve(path);
    char target[PATH_MAX];
    ssize_t n = readlink(full.c_str(), target, sizeof(target) - 1);
    if (n < 0) {
        SendStatusFromErrno(id);
        return;
    }
    target[n] = '\0';
    std::vector<std::string> names{target};
    std::vector<Attributes> attrs{Attributes()};
    SendName(id, names, attrs, false);
}

void HandleSymlink(uint32_t id, Reader& r) {
    // SFTP v3 (OpenSSH) order is: linkpath(symlink), target.
    std::string linkpath;
    std::string target;
    if (!r.String(&linkpath) || !r.String(&target)) {
        SendStatus(id, kStatusBadMessage, "bad symlink request");
        return;
    }
    if (symlink(target.c_str(), Resolve(linkpath).c_str()) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    SendStatus(id, kStatusOk, "ok");
}

void HandleRealpath(uint32_t id, Reader& r) {
    std::string path;
    if (!r.String(&path)) {
        SendStatus(id, kStatusBadMessage, "bad realpath request");
        return;
    }
    std::string full = Resolve(path);
    struct stat st;
    Attributes a;
    if (stat(full.c_str(), &st) == 0) {
        a = FromStat(st);
    }
    std::vector<std::string> names{full};
    std::vector<Attributes> attrs{a};
    SendName(id, names, attrs, false);
}

void SendStatvfs(uint32_t id, const std::string& path) {
    struct statvfs st;
    std::string full = Resolve(path);
    if (statvfs(full.c_str(), &st) != 0) {
        SendStatusFromErrno(id);
        return;
    }
    Writer w;
    w.U8(kFxpExtendedReply);
    w.U32(id);
    w.U64(st.f_bsize);
    w.U64(st.f_frsize);
    w.U64(st.f_blocks);
    w.U64(st.f_bfree);
    w.U64(st.f_bavail);
    w.U64(st.f_files);
    w.U64(st.f_ffree);
    w.U64(st.f_favail);
    w.U64(st.f_fsid);
    w.U64(st.f_flag);
    w.U64(st.f_namemax);
    SendPacket(w);
}

void HandleExtended(uint32_t id, Reader& r) {
    std::string request;
    if (!r.String(&request)) {
        SendStatus(id, kStatusBadMessage, "bad extended request");
        return;
    }
    if (request == "posix-rename@openssh.com") {
        std::string oldpath;
        std::string newpath;
        if (!r.String(&oldpath) || !r.String(&newpath)) {
            SendStatus(id, kStatusBadMessage, "bad posix-rename request");
            return;
        }
        if (rename(Resolve(oldpath).c_str(), Resolve(newpath).c_str()) != 0) {
            SendStatusFromErrno(id);
            return;
        }
        SendStatus(id, kStatusOk, "ok");
        return;
    }
    if (request == "statvfs@openssh.com") {
        std::string path;
        if (!r.String(&path)) {
            SendStatus(id, kStatusBadMessage, "bad statvfs request");
            return;
        }
        SendStatvfs(id, path);
        return;
    }
    if (request == "hardlink@openssh.com") {
        std::string oldpath;
        std::string newpath;
        if (!r.String(&oldpath) || !r.String(&newpath)) {
            SendStatus(id, kStatusBadMessage, "bad hardlink request");
            return;
        }
        if (link(Resolve(oldpath).c_str(), Resolve(newpath).c_str()) != 0) {
            SendStatusFromErrno(id);
            return;
        }
        SendStatus(id, kStatusOk, "ok");
        return;
    }
    SendStatus(id, kStatusOpUnsupported, "unsupported extension");
}

void SendVersion() {
    Writer w;
    w.U8(kFxpVersion);
    w.U32(3);
    static const struct {
        const char* name;
        const char* version;
    } kExtensions[] = {
        {"posix-rename@openssh.com", "1"},
        {"statvfs@openssh.com", "2"},
        {"hardlink@openssh.com", "1"},
    };
    for (const auto& ext : kExtensions) {
        w.String(ext.name);
        w.String(ext.version);
    }
    SendPacket(w);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    bool initialized = false;
    while (true) {
        uint8_t header[4];
        if (!ReadFull(STDIN_FILENO, header, sizeof(header))) {
            break;
        }
        uint32_t len = GetU32(header);
        if (len < 1 || len > kMaxPacket) {
            break;
        }
        std::vector<uint8_t> payload(len);
        if (!ReadFull(STDIN_FILENO, payload.data(), len)) {
            break;
        }

        uint8_t type = payload[0];
        uint32_t id = 0;
        Reader r(payload, 1);
        if (type != kFxpInit) {
            if (!r.U32(&id)) {
                break;
            }
        }

        if (type == kFxpInit) {
            if (initialized) break;
            initialized = true;
            SendVersion();
            continue;
        }
        if (!initialized) {
            break;
        }

        switch (type) {
            case kFxpOpen: HandleOpen(id, r); break;
            case kFxpClose: HandleClose(id, r); break;
            case kFxpRead: HandleRead(id, r); break;
            case kFxpWrite: HandleWrite(id, r); break;
            case kFxpLstat: HandleStat(id, r, false); break;
            case kFxpStat: HandleStat(id, r, true); break;
            case kFxpFstat: HandleFstat(id, r); break;
            case kFxpSetstat: HandleSetstat(id, r); break;
            case kFxpFsetstat: HandleFsetstat(id, r); break;
            case kFxpOpendir: HandleOpendir(id, r); break;
            case kFxpReaddir: HandleReaddir(id, r); break;
            case kFxpRemove: HandleRemove(id, r); break;
            case kFxpMkdir: HandleMkdir(id, r); break;
            case kFxpRmdir: HandleRmdir(id, r); break;
            case kFxpRealpath: HandleRealpath(id, r); break;
            case kFxpRename: HandleRename(id, r); break;
            case kFxpReadlink: HandleReadlink(id, r); break;
            case kFxpSymlink: HandleSymlink(id, r); break;
            case kFxpExtended: HandleExtended(id, r); break;
            default:
                SendStatus(id, kStatusOpUnsupported, "unsupported operation");
                break;
        }
    }

    for (auto& entry : g_handles) {
        delete entry.second;
    }
    g_handles.clear();
    return 0;
}
