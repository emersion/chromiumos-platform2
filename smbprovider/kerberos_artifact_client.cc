// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/kerberos_artifact_client.h"

#include <memory>
#include <utility>

#include <dbus/authpolicy/dbus-constants.h>
#include <dbus/message.h>

namespace smbprovider {
namespace {

authpolicy::ErrorType GetErrorFromReader(dbus::MessageReader* reader) {
  int32_t int_error;
  if (!reader->PopInt32(&int_error)) {
    DLOG(ERROR)
        << "KerberosArtifactClient: Failed to get an error from the response";
    return authpolicy::ERROR_DBUS_FAILURE;
  }
  if (int_error < 0 || int_error >= authpolicy::ERROR_COUNT) {
    return authpolicy::ERROR_UNKNOWN;
  }
  return static_cast<authpolicy::ErrorType>(int_error);
}

authpolicy::ErrorType GetErrorAndProto(
    dbus::Response* response, google::protobuf::MessageLite* protobuf) {
  if (!response) {
    DLOG(ERROR) << "KerberosArtifactClient: Failed to  call to authpolicy";
    return authpolicy::ERROR_DBUS_FAILURE;
  }
  dbus::MessageReader reader(response);
  const authpolicy::ErrorType error(GetErrorFromReader(&reader));

  if (error != authpolicy::ERROR_NONE) {
    return error;
  }

  if (!reader.PopArrayOfBytesAsProto(protobuf)) {
    DLOG(ERROR) << "Failed to parse protobuf.";
    return authpolicy::ERROR_DBUS_FAILURE;
  }
  return authpolicy::ERROR_NONE;
}

}  // namespace

KerberosArtifactClient::KerberosArtifactClient(
    brillo::dbus_utils::DBusObject* dbus_object)
    : weak_ptr_factory_(this) {
  auth_policy_object_proxy_ = dbus_object->GetBus()->GetObjectProxy(
      authpolicy::kAuthPolicyServiceName,
      dbus::ObjectPath(authpolicy::kAuthPolicyServicePath));
}

void KerberosArtifactClient::GetUserKerberosFiles(
    const std::string& object_guid, GetUserKerberosFilesCallback callback) {
  dbus::MethodCall method_call(authpolicy::kAuthPolicyInterface,
                               authpolicy::kGetUserKerberosFilesMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(object_guid);
  auth_policy_object_proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(&KerberosArtifactClient::HandleGetUserKeberosFiles,
                 weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void KerberosArtifactClient::ConnectToKerberosFilesChangedSignal(
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
  auth_policy_object_proxy_->ConnectToSignal(
      authpolicy::kAuthPolicyInterface,
      authpolicy::kUserKerberosFilesChangedSignal, std::move(signal_callback),
      std::move(on_connected_callback));
}

void KerberosArtifactClient::HandleGetUserKeberosFiles(
    GetUserKerberosFilesCallback callback, dbus::Response* response) {
  authpolicy::KerberosFiles files_proto;
  authpolicy::ErrorType error(GetErrorAndProto(response, &files_proto));

  callback.Run(error, files_proto);
}

}  // namespace smbprovider
