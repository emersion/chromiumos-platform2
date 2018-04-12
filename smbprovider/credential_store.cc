// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/credential_store.h"

#include <libpasswordprovider/password.h>

namespace smbprovider {

namespace {

// Returns true if |buffer_length| is large enough to contain |str|.
bool CanBufferHoldString(const std::string& str, int32_t buffer_length) {
  return static_cast<int32_t>(str.size()) + 1 <= buffer_length;
}

// Returns true if |buffer_length| is large enough to contain |password|.
bool CanBufferHoldPassword(
    const std::unique_ptr<password_provider::Password>& password,
    int32_t buffer_length) {
  DCHECK(password);

  return static_cast<int32_t>(password->size()) + 1 <= buffer_length;
}

// Copies |str| to |buffer| and adds a null terminator at the end.
void CopyStringToBuffer(const std::string& str, char* buffer) {
  DCHECK(buffer);

  strncpy(buffer, str.c_str(), str.size());
  buffer[str.size()] = '\0';
}

// Copies |password| to |buffer| and adds a null terminator at the end.
void CopyPasswordToBuffer(
    const std::unique_ptr<password_provider::Password>& password,
    char* buffer) {
  DCHECK(password);
  DCHECK(buffer);

  strncpy(buffer, password->GetRaw(), password->size());
  buffer[password->size()] = '\0';
}

// Checks that the credentials can be inputted given the buffer sizes. Returns
// false if the buffers are too small or if the credentials are empty.
bool CanInputCredentials(int32_t workgroup_length,
                         int32_t username_length,
                         int32_t password_length,
                         const SmbCredentials& credentials) {
  const std::string& workgroup = credentials.workgroup;
  const std::string& username = credentials.username;
  const bool empty_password = !credentials.password;

  if (workgroup.empty() && username.empty() && empty_password) {
    return false;
  }

  if (!CanBufferHoldString(workgroup, workgroup_length) ||
      !CanBufferHoldString(username, username_length)) {
    LOG(ERROR) << "Credential buffers are too small for input.";
    return false;
  }

  if (!empty_password &&
      !CanBufferHoldPassword(credentials.password, password_length)) {
    LOG(ERROR) << "Password buffer is too small for input.";
    return false;
  }

  return true;
}

// Sets the |credentials| into the specified buffers. CanInputCredentials()
// should be called first in order to verify the buffers can contain the
// credentials.
void SetCredentials(const SmbCredentials& credentials,
                    char* workgroup_buffer,
                    char* username_buffer,
                    char* password_buffer) {
  DCHECK(workgroup_buffer);
  DCHECK(username_buffer);
  DCHECK(password_buffer);

  CopyStringToBuffer(credentials.workgroup, workgroup_buffer);
  CopyStringToBuffer(credentials.username, username_buffer);

  if (credentials.password) {
    CopyPasswordToBuffer(credentials.password, password_buffer);
  }
}

}  // namespace

CredentialStore::CredentialStore() = default;
CredentialStore::~CredentialStore() = default;

void CredentialStore::GetAuthentication(const std::string& share_path,
                                        char* workgroup,
                                        int32_t workgroup_length,
                                        char* username,
                                        int32_t username_length,
                                        char* password,
                                        int32_t password_length) const {
  if (!HasCredentials(share_path)) {
    return;
  }

  const SmbCredentials& credentials = GetCredentials(share_path);
  if (!CanInputCredentials(workgroup_length, username_length, password_length,
                           credentials)) {
    return;
  }

  SetCredentials(credentials, workgroup, username, password);
}

}  // namespace smbprovider