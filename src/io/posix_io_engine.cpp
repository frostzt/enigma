#include "enigmadb/io/posix_io_engine.h"

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/io/io_engine.h"

using namespace enigmadb::common;

namespace enigmadb::io {

#ifndef NDEBUG

#if defined(__linux__)
#include <dirent.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#endif

static bool debug_has_open_fd_for_path(const std::string& target_path) {
#if defined(__linux__)
    int dir_fd = ::open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
    if (dir_fd == -1) return false;

    DIR* dir = ::fdopendir(dir_fd);
    if (!dir) {
        ::close(dir_fd);
        return false;
    }

    struct dirent* entry;
    char link_buf[PATH_MAX];
    bool leaked = false;

    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string fd_path = std::string("/proc/self/fd/") + entry->d_name;
        ssize_t len =
            ::readlink(fd_path.c_str(), link_buf, sizeof(link_buf) - 1);
        if (len != -1) {
            link_buf[len] = '\0';
            std::string resolved(link_buf);

            // Linux appends " (deleted)" to unlinked open files
            if (resolved == target_path ||
                resolved == (target_path + " (deleted)")) {
                leaked = true;
                break;
            }
        }
    }

    ::closedir(dir);
    return leaked;
#elif defined(__APPLE__)
    pid_t pid = ::getpid();

    int buffer_size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (buffer_size <= 0) return false;

    std::vector<struct proc_fdinfo> fds(buffer_size /
                                        sizeof(struct proc_fdinfo));
    buffer_size =
        proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), buffer_size);
    int fd_count = buffer_size / sizeof(struct proc_fdinfo);

    for (int i = 0; i < fd_count; ++i) {
        if (fds[i].proc_fdtype == PROX_FDTYPE_VNODE) {
            struct vnode_fdinfowithpath vnode_path_info;
            int ret =
                proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO,
                               &vnode_path_info, sizeof(vnode_path_info));
            if (ret > 0) {
                std::string resolved_path(vnode_path_info.pvip.vip_path);
                if (resolved_path == target_path) {
                    return true;
                }
            }
        }
    }
    return false;
#else
    return false;
#endif
}

#endif  // NDEBUG

IOResult<FileHandle> PosixIOEngine::open(const std::string& path, Mode mode) {
    // clang-format off
    int flags;
    switch (mode) {
      case Mode::Read:      flags = O_RDONLY;                        break;
      case Mode::Write:     flags = O_WRONLY | O_CREAT;              break;
      case Mode::ReadWrite: flags = O_RDWR   | O_CREAT;              break;
      case Mode::Append:    flags = O_WRONLY | O_APPEND | O_CREAT;   break;
      case Mode::Overwrite: flags = O_WRONLY | O_CREAT | O_TRUNC;    break;
    }
    // clang-format on

    errno = 0;
    int fd = ::open(path.c_str(), flags, 0644);
    if (fd == -1) {
        char* err_msg = strerror(errno);
        return ExpectResult<FileHandle, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, err_msg});
    }

    /* We use 'construct_tag' here as we know PosixIOEngine is a concrete impl
     */
    auto fh = FileHandle(FileHandle::construct_tag{}, fd);
    return ExpectResult<FileHandle, Error>::ok(std::move(fh));
};

IOResult<void> PosixIOEngine::sync_data(const FileHandle& fh) {
    if (fh.fd() == -1) {
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, "invalid file descriptor"});
    }

    errno = 0;
#ifdef __linux__
    int ret = ::fdatasync(fh.fd());
#elif defined(__APPLE__)
    int ret = fsync(fh.fd());
#endif
    if (ret == -1) {
        char* err_msg = strerror(errno);
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FSYNC_ERR, err_msg});
    }
    return ExpectResult<void, Error>::ok();
}

IOResult<void> PosixIOEngine::sync_all(const FileHandle& fh) {
    if (fh.fd() == -1) {
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, "invalid file descriptor"});
    }
    errno = 0;
#ifdef __linux__
    int ret = ::fsync(fh.fd());
#elif defined(__APPLE__)
    int ret = fcntl(fh.fd(), F_FULLFSYNC);
#endif
    if (ret == -1) {
        char* err_msg = strerror(errno);
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FSYNC_ERR, err_msg});
    }
    return ExpectResult<void, Error>::ok();
}

IOResult<void> PosixIOEngine::sync_directory(const std::string& path) {
    errno = 0;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        char* err_msg = strerror(errno);
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, err_msg});
    }
    FileHandle dir_handle(FileHandle::construct_tag{}, fd);
    if (fsync(fd) == -1) {
        char* err_msg = strerror(errno);
        return ExpectResult<void, Error>::err(
            Error{ErrorCode::FSYNC_ERR, err_msg});
    }
    return ExpectResult<void, Error>::ok();
}

IOResult<size_t> PosixIOEngine::append(const FileHandle& fh,
                                       const uint8_t* buffer, size_t length) {
    errno = 0;
    if (fh.fd() == -1) {
        return ExpectResult<size_t, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, "invalid file descriptor"});
    }
    size_t bytes_written = 0;
    while (bytes_written < length) {
        ssize_t bytes =
            ::write(fh.fd(), buffer + bytes_written, length - bytes_written);
        if (bytes == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                char* err_msg = strerror(errno);
                return ExpectResult<size_t, Error>::err(
                    Error{ErrorCode::WRITE_ERR, err_msg});
            }
        }
        bytes_written += bytes;
    }
    return bytes_written;
}

IOResult<size_t> PosixIOEngine::read(const FileHandle& fh, size_t count,
                                     uint8_t* buffer, size_t offset) {
    if (fh.fd() == -1) {
        return ExpectResult<size_t, Error>::err(
            Error{ErrorCode::FILE_DESCRIPTOR_ERR, "invalid file descriptor"});
    }
    errno = 0;
    size_t bytes_read = 0;
    while (bytes_read < count) {
        ssize_t bytes = ::pread(fh.fd(), buffer + bytes_read,
                                count - bytes_read, offset + bytes_read);
        if (bytes == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                char* err_msg = strerror(errno);
                return ExpectResult<size_t, Error>::err(
                    Error{ErrorCode::READ_ERR, err_msg});
            }
        } else if (bytes == 0) { /* eof */
            break;
        }
        bytes_read += bytes;
    }

    return bytes_read;
}

IOResult<size_t> PosixIOEngine::file_size(const FileHandle& fh) {
    struct stat st;
    errno = 0;
    if (::fstat(fh.fd(), &st) == -1) {
        char* err_msg = strerror(errno);
        return IOResult<size_t>::err(Error{ErrorCode::FSTAT_ERR, err_msg});
    }
    return IOResult<size_t>::ok(static_cast<size_t>(st.st_size));
}

IOResult<void> PosixIOEngine::remove(const std::string& path) {
    errno = 0;

    /* Remove the provided file */
    if (::unlink(path.c_str()) == -1) {
        if (errno == ENOENT) {
            char* err_msg = ::strerror(errno);
            return ExpectResult<void, Error>::err(
                Error{ErrorCode::FILE_DESCRIPTOR_ERR, err_msg});
        }
    }

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        /* Best effort parent dir sync */
        sync_directory(p.parent_path().string());
    }

#ifndef NDEBUG
    /*  Path must no longer exist in directory hierarchy */
    assert(!std::filesystem::exists(path) &&
           "File path still exists after unlink!");
#if defined(__linux__) || defined(__APPLE__)
    /* Ensure this process isn't holding onto an open FD for this file */
    bool leaked = debug_has_open_fd_for_path(path);
    assert(!leaked &&
           "FS LEAK: Attempted to remove file while process still holds an "
           "open FileHandle/FD!");
#endif
#endif
    return IOResult<void>::ok();
}

}  // namespace enigmadb::io
