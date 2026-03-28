#include <cerrno>
#include <sys/fcntl.h>
#include <unistd.h>

#include "enigmadb/common/error.h"
#include "enigmadb/common/result.h"
#include "enigmadb/io/io_engine.hpp"
#include "enigmadb/io/posix_io_engine.hpp"

namespace enigmadb::io {

ExpectResult<FileHandle, Error> PosixIOEngine::open(const std::string &path,
                                                    Mode mode) {
  // clang-format off
  int flags;
  switch (mode) {
    case Mode::Read:      flags = O_RDONLY;                        break;
    case Mode::Write:     flags = O_WRONLY | O_CREAT;              break;
    case Mode::Append:    flags = O_WRONLY | O_APPEND | O_CREAT;   break;
    case Mode::Overwrite: flags = O_WRONLY | O_CREAT | O_TRUNC;    break;
  }
  // clang-format on

  errno = 0;
  int fd = ::open(path.c_str(), flags, 0644);
  if (fd == -1) {
    char *err_msg = strerror(errno);
    return ExpectResult<FileHandle, Error>::err(
        Error{ErrorCode::FILE_DESCRIPTER_ERR, err_msg});
  }

  /* We use 'construct_tag' here as we know PosixIOEngine is a concrete impl */
  auto fh = FileHandle(FileHandle::construct_tag{}, fd);
  return ExpectResult<FileHandle, Error>::ok(std::move(fh));
};

ExpectResult<void, Error> PosixIOEngine::sync_data(const FileHandle &fh) {
  if (fh.fd() != -1) {
    errno = 0;
#ifdef __linux__
    int ret = ::fdatasync(fh.fd());
#elif defined(__APPLE__)
    int ret = fsync(fh.fd());
#endif
    if (ret == -1) {
      char *err_msg = strerror(errno);
      return ExpectResult<void, Error>::err(
          Error{ErrorCode::FSYNC_ERR, err_msg});
    }
  }
  return ExpectResult<void, Error>::ok();
}

ExpectResult<void, Error> PosixIOEngine::sync_all(const FileHandle &fh) {
  if (fh.fd() != -1) {
    errno = 0;
#ifdef __linux__
    int ret = ::fsync(fh.fd());
#elif defined(__APPLE__)
    int ret = fcntl(fh.fd(), F_FULLFSYNC);
#endif
    if (ret == -1) {
      char *err_msg = strerror(errno);
      return ExpectResult<void, Error>::err(
          Error{ErrorCode::FSYNC_ERR, err_msg});
    }
  }
  return ExpectResult<void, Error>::ok();
}

ExpectResult<void, Error>
PosixIOEngine::sync_directory(const std::string &path) {
  errno = 0;
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fsync(fd) == -1) {
    char *err_msg = strerror(errno);
    return ExpectResult<void, Error>::err(Error{ErrorCode::FSYNC_ERR, err_msg});
  }
  if (close(fd) == -1) {
    char *err_msg = strerror(errno);
    return ExpectResult<void, Error>::err(Error{ErrorCode::CLOSE_ERR, err_msg});
  }
  return ExpectResult<void, Error>::ok();
}

} // namespace enigmadb::io
