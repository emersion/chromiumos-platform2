// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/smbprovider_helper.h"

#include <errno.h>

#include <base/bits.h>
#include <base/files/file_util.h>
#include <base/strings/string_piece.h>
#include <dbus/file_descriptor.h>
#include <libsmbclient.h>

#include "smbprovider/constants.h"
#include "smbprovider/proto.h"

namespace smbprovider {

void AddEntryIfValid(const smbc_dirent& dirent,
                     std::vector<DirectoryEntry>* directory_entries,
                     const std::string& parent_dir_path) {
  const std::string name(dirent.name);
  // Ignore "." and ".." entries.
  // TODO(allenvic): Handle SMBC_LINK
  if (IsSelfOrParentDir(name) || !ShouldProcessEntryType(dirent.smbc_type)) {
    return;
  }
  bool is_directory = dirent.smbc_type == SMBC_DIR;
  directory_entries->emplace_back(is_directory, name,
                                  AppendPath(parent_dir_path, name));
}

smbc_dirent* AdvanceDirEnt(smbc_dirent* dirp) {
  DCHECK(dirp);
  DCHECK_GE(dirp->dirlen, sizeof(smbc_dirent));
  return reinterpret_cast<smbc_dirent*>(reinterpret_cast<uint8_t*>(dirp) +
                                        dirp->dirlen);
}

std::string AppendPath(const std::string& base_path,
                       const std::string& relative_path) {
  const base::FilePath path(base_path);
  base::FilePath relative(relative_path);
  if (relative.IsAbsolute() && relative_path.size() > 0) {
    // Remove the beginning "/" since FilePath#Append() cannot append an
    // 'absolute' path.
    relative = base::FilePath(
        base::StringPiece(relative_path.c_str() + 1, relative_path.size() - 1));
  }
  return path.Append(relative).value();
}

size_t CalculateEntrySize(const std::string& entry_name) {
  return base::bits::Align(sizeof(smbc_dirent) + entry_name.size(),
                           alignof(smbc_dirent));
}

smbc_dirent* GetDirentFromBuffer(uint8_t* buffer) {
  return reinterpret_cast<smbc_dirent*>(buffer);
}

bool IsSelfOrParentDir(const std::string& entry_name) {
  return entry_name == kEntrySelf || entry_name == kEntryParent;
}

bool ShouldProcessEntryType(uint32_t smbc_type) {
  return smbc_type == SMBC_FILE || smbc_type == SMBC_DIR;
}

bool WriteEntry(const std::string& entry_name,
                int32_t entry_type,
                size_t buffer_size,
                smbc_dirent* dirp) {
  DCHECK(dirp);
  size_t entry_size = CalculateEntrySize(entry_name);
  if (entry_size > buffer_size) {
    return false;
  }
  dirp->smbc_type = entry_type;
  dirp->dirlen = entry_size;
  memcpy(dirp->name, entry_name.c_str(), entry_name.size() + 1);
  return true;
}

ErrorType GetErrorFromErrno(int32_t error_code) {
  DCHECK_GT(error_code, 0);
  ErrorType error;
  switch (error_code) {
    case EPERM:
    case EACCES:
      error = ERROR_ACCESS_DENIED;
      break;
    case EBADF:
    case ENODEV:
    case ENOENT:
      error = ERROR_NOT_FOUND;
      break;
    case EMFILE:
    case ENFILE:
      error = ERROR_TOO_MANY_OPENED;
      break;
    case ENOTDIR:
      error = ERROR_NOT_A_DIRECTORY;
      break;
    case EISDIR:
      error = ERROR_NOT_A_FILE;
      break;
    case ENOTEMPTY:
      error = ERROR_NOT_EMPTY;
      break;
    case EEXIST:
      error = ERROR_EXISTS;
      break;
    case EINVAL:
      error = ERROR_INVALID_OPERATION;
      break;
    default:
      error = ERROR_FAILED;
      break;
  }
  return error;
}

bool IsDirectory(const struct stat& stat_info) {
  return S_ISDIR(stat_info.st_mode);
}

bool IsFile(const struct stat& stat_info) {
  return S_ISREG(stat_info.st_mode);
}

void GetValidDBusFD(base::ScopedFD* fd, dbus::FileDescriptor* dbus_fd) {
  DCHECK(dbus_fd);
  DCHECK(fd);
  DCHECK(fd->is_valid());
  dbus_fd->PutValue(fd->release());
  dbus_fd->CheckValidity();
  DCHECK(dbus_fd->is_valid());
}

void LogAndSetError(const char* operation_name,
                    int32_t mount_id,
                    ErrorType error_received,
                    int32_t* error_code) {
  LOG(ERROR) << "Error performing " << operation_name
             << " from mount id: " << mount_id << ": " << error_received;
  *error_code = static_cast<int32_t>(error_received);
}

void LogAndSetDBusParseError(const char* operation_name, int32_t* error_code) {
  LogAndSetError(operation_name, -1, ERROR_DBUS_PARSE_FAILED, error_code);
}

int32_t GetDirectoryEntryProtoFromStat(const std::string& full_path,
                                       const struct stat& stat_info,
                                       ProtoBlob* proto_blob) {
  DCHECK(proto_blob);
  bool is_directory = IsDirectory(stat_info);
  int64_t size = is_directory ? 0 : stat_info.st_size;
  const base::FilePath path(full_path);

  DirectoryEntryProto entry;
  entry.set_is_directory(is_directory);
  entry.set_name(path.BaseName().value());
  entry.set_size(size);
  entry.set_last_modified_time(stat_info.st_mtime);
  return static_cast<int32_t>(SerializeProtoToBlob(entry, proto_blob));
}

bool IsValidOpenFileFlags(int32_t flags) {
  return flags == O_RDONLY || flags == O_RDWR || flags == O_WRONLY;
}

bool ReadFromFD(const WriteFileOptionsProto& options,
                const dbus::FileDescriptor& fd,
                int32_t* error,
                std::vector<uint8_t>* buffer) {
  DCHECK(buffer);
  DCHECK(error);

  if (!fd.is_valid()) {
    LogAndSetError(options, ERROR_DBUS_PARSE_FAILED, error);
    return false;
  }

  buffer->resize(options.length());
  if (!base::ReadFromFD(fd.value(), reinterpret_cast<char*>(buffer->data()),
                        buffer->size())) {
    LogAndSetError(options, ERROR_IO, error);
    return false;
  }

  return true;
}

int32_t GetOpenFilePermissions(const OpenFileOptionsProto& options) {
  return options.writeable() ? O_RDWR : O_RDONLY;
}

int32_t GetOpenFilePermissions(const TruncateOptionsProto& unused) {
  return O_WRONLY;
}

int32_t GetOpenFilePermissions(const CopyEntryOptionsProto& unused) {
  // OpenFile is Read-Only for CopyEntry since we only need to read the source.
  return O_RDONLY;
}

PathParts SplitPath(const std::string& full_path) {
  DCHECK(!full_path.empty());
  base::FilePath path(full_path);
  std::vector<std::string> result;
  path.GetComponents(&result);
  return result;
}

std::string RemoveURLScheme(const std::string& smb_url) {
  DCHECK_EQ(0, smb_url.compare(0, 6, kSmbUrlScheme));
  return smb_url.substr(5, std::string::npos);
}

std::string GetFileName(const std::string& full_path) {
  base::FilePath file_path(RemoveURLScheme(full_path));
  return file_path.BaseName().value();
}

std::string GetDirPath(const std::string& full_path) {
  std::string path = RemoveURLScheme(full_path);
  return base::FilePath(path).DirName().value();
}

bool ShouldReportCreateDirError(int32_t result, bool ignore_existing) {
  if (result == 0) {
    return false;
  }
  return !(result == EEXIST && ignore_existing);
}

}  // namespace smbprovider
