// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>

#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <seneschal/proto_bindings/seneschal_service.pb.h>
#include <vm_concierge/proto_bindings/service.pb.h>

using std::string;

namespace {

int StartServer(dbus::ObjectProxy* proxy, uint64_t port, uint64_t accept_cid) {
  if (port == 0) {
    LOG(ERROR) << "--port is required";
    return EXIT_FAILURE;
  }

  if (port > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    LOG(ERROR) << "--port value is too large; maximum value allowed is "
               << std::numeric_limits<uint32_t>::max();
    return EXIT_FAILURE;
  }

  if (accept_cid < 3) {
    LOG(ERROR) << "invalid value for --accept_cid: " << accept_cid;
    return EXIT_FAILURE;
  }
  if (accept_cid >
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    LOG(ERROR) << "--accept_cid value is too large; maximum value allowed is "
               << std::numeric_limits<uint32_t>::max();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Starting server";

  dbus::MethodCall method_call(vm_tools::seneschal::kSeneschalInterface,
                               vm_tools::seneschal::kStartServerMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::seneschal::StartServerRequest request;
  request.mutable_vsock()->set_port(static_cast<uint32_t>(port));
  request.mutable_vsock()->set_accept_cid(static_cast<uint32_t>(accept_cid));
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode StartServerRequest protobuf";
    return EXIT_FAILURE;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to seneschal service";
    return EXIT_FAILURE;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::seneschal::StartServerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response protobuf";
    return EXIT_FAILURE;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to start server: " << response.failure_reason();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Started server with handle: " << response.handle();
  return EXIT_SUCCESS;
}

int StopServer(dbus::ObjectProxy* proxy, uint64_t handle) {
  if (handle == 0) {
    LOG(ERROR) << "--handle is required";
    return EXIT_FAILURE;
  }

  if (handle > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    LOG(ERROR) << "--handle value is too large; maximum value allowed is "
               << std::numeric_limits<uint32_t>::max();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Stopping server " << handle;
  dbus::MethodCall method_call(vm_tools::seneschal::kSeneschalInterface,
                               vm_tools::seneschal::kStopServerMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::seneschal::StopServerRequest request;
  request.set_handle(static_cast<uint32_t>(handle));

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode StopServerRequest protobuf";
    return EXIT_FAILURE;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to seneschal service";
    return EXIT_FAILURE;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::seneschal::StopServerResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response protobuf";
    return EXIT_FAILURE;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to stop server: " << response.failure_reason();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Stopped server " << handle;
  return EXIT_SUCCESS;
}

int SharePath(dbus::ObjectProxy* proxy,
              uint64_t handle,
              string owner_id,
              string path,
              bool writable) {
  if (handle == 0) {
    LOG(ERROR) << "--handle is required";
    return EXIT_FAILURE;
  }

  if (handle > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    LOG(ERROR) << "--handle value is too large; maximum value allowed is "
               << std::numeric_limits<uint32_t>::max();
    return EXIT_FAILURE;
  }

  if (path.empty()) {
    LOG(ERROR) << "--path is required";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Sharing " << path << " with server " << handle;

  dbus::MethodCall method_call(vm_tools::seneschal::kSeneschalInterface,
                               vm_tools::seneschal::kSharePathMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::seneschal::SharePathRequest request;
  request.set_handle(static_cast<uint32_t>(handle));
  request.set_owner_id(std::move(owner_id));
  request.set_storage_location(
      vm_tools::seneschal::SharePathRequest::DOWNLOADS);

  vm_tools::seneschal::SharedPath* shared_path = request.mutable_shared_path();
  shared_path->set_path(std::move(path));
  shared_path->set_writable(writable);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SharePathRequest protobuf";
    return EXIT_FAILURE;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to seneschal service";
    return EXIT_FAILURE;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::seneschal::SharePathResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response protobuf";
    return EXIT_FAILURE;
  }

  if (!response.success()) {
    std::cout << "Unable to share path: " << response.failure_reason()
              << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << request.shared_path().path() << " is available at path "
            << "/mnt/shared" << response.path() << std::endl;
  return EXIT_SUCCESS;
}

bool GetServerHandle(scoped_refptr<dbus::Bus> bus,
                     const string& owner_id,
                     string vm_name,
                     uint64_t* handle) {
  dbus::ObjectProxy* concierge_proxy = bus->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));
  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kGetVmInfoMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::concierge::GetVmInfoRequest request;
  request.set_owner_id(owner_id);
  request.set_name(vm_name);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode GetVmInfo protobuf";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      concierge_proxy->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to concierge service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::concierge::GetVmInfoResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response protobuf";
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to get VM info for " << vm_name;
    return false;
  }

  *handle = response.vm_info().seneschal_server_handle();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;

  // Operations.
  DEFINE_bool(start, false, "Start a new server");
  DEFINE_bool(stop, false, "Stop a running server");
  DEFINE_bool(share_path, false, "Share a path with a running server");

  // Parameters.
  DEFINE_string(vm_name, "", "The name for the VM");
  DEFINE_string(owner_id, "", "The cryptohome id of the user");
  DEFINE_uint64(handle, 0, "The handle for the server");
  DEFINE_uint64(port, 0, "Port number on which the server should listen");
  DEFINE_uint64(
      accept_cid, 0,
      "The vsock context id from which the server should accept connections");
  DEFINE_string(path, "", "Path to share with a running server");
  DEFINE_bool(writable, false, "Whether the shared path should be writable");

  brillo::FlagHelper::Init(argc, argv, "seneschal client tool");
  brillo::InitLog(brillo::kLogToStderrIfTty);

  base::MessageLoopForIO message_loop;

  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return EXIT_FAILURE;
  }

  dbus::ObjectProxy* proxy = bus->GetObjectProxy(
      vm_tools::seneschal::kSeneschalServiceName,
      dbus::ObjectPath(vm_tools::seneschal::kSeneschalServicePath));
  if (!proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::seneschal::kSeneschalServiceName;
    return EXIT_FAILURE;
  }

  if (FLAGS_start + FLAGS_stop + FLAGS_share_path != 1) {
    LOG(ERROR) << "Exactly one of --start, --stop, or --share_path is required";
    return EXIT_FAILURE;
  }

  if (FLAGS_start) {
    return StartServer(proxy, FLAGS_port, FLAGS_accept_cid);
  } else if (FLAGS_stop) {
    return StopServer(proxy, FLAGS_handle);
  } else if (FLAGS_share_path) {
    if (FLAGS_handle == 0 && FLAGS_vm_name.empty()) {
      LOG(ERROR) << "--handle or --vm_name is required";
      return EXIT_FAILURE;
    }

    if (FLAGS_owner_id.empty()) {
      LOG(ERROR) << "--owner_id is required";
      return EXIT_FAILURE;
    }

    uint64_t handle = FLAGS_handle;
    if (handle == 0 && !GetServerHandle(bus, FLAGS_owner_id,
                                        std::move(FLAGS_vm_name), &handle)) {
      LOG(ERROR) << "Failed to get server handle";
      return EXIT_FAILURE;
    }

    return SharePath(proxy, handle, std::move(FLAGS_owner_id),
                     std::move(FLAGS_path), FLAGS_writable);
  }

  NOTREACHED();
  return EXIT_FAILURE;
}