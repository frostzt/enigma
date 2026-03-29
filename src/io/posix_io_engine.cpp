#include "enigmadb/io/posix_io_engine.hpp"

#include <sys/fcntl.h>
#include <unistd.h>

#include <cerrno>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/io/io_engine.hpp"

namespace enigmadb::io {

ExpectResult<FileHandle, Error> PosixIOEngine::open(const std::string& path,
                                                    Mode mode) {
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

ExpectResult<void, Error> PosixIOEngine::sync_data(const FileHandle& fh) {
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

ExpectResult<void, Error> PosixIOEngine::sync_all(const FileHandle& fh) {
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

ExpectResult<void, Error> PosixIOEngine::sync_directory(
    const std::string& path) {
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

ExpectResult<size_t, Error> PosixIOEngine::append(const FileHandle& fh,
                                                  const char* buffer,
                                                  size_t length) {
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

ExpectResult<size_t, Error> PosixIOEngine::read(const FileHandle& fh,
                                                size_t count, char* buffer,
                                                size_t offset) {
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

}  // namespace enigmadb::io
