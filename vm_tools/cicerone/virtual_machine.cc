// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/virtual_machine.h"

#include <arpa/inet.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <google/protobuf/repeated_field.h>
#include <grpc++/grpc++.h>

#include "container_guest.grpc.pb.h"  // NOLINT(build/include)
#include "vm_tools/common/constants.h"

using std::string;

namespace vm_tools {
namespace cicerone {
namespace {

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 2;

}  // namespace

VirtualMachine::VirtualMachine(uint32_t container_subnet,
                               uint32_t container_netmask)
    : container_subnet_(container_subnet),
      container_netmask_(container_netmask) {}

VirtualMachine::~VirtualMachine() = default;

bool VirtualMachine::RegisterContainer(const std::string& container_token,
                                       const std::string& container_ip) {
  // The token will be in the pending map if this is the first start of the
  // container. It will be in the main map if this is from a crash/restart of
  // the garcon process in the container.
  std::string container_name;
  auto iter = pending_container_token_to_name_.find(container_token);
  if (iter != pending_container_token_to_name_.end()) {
    container_name = iter->second;
    container_token_to_name_[container_token] = container_name;
    pending_container_token_to_name_.erase(iter);
  } else {
    container_name = GetContainerNameForToken(container_token);
    if (container_name.empty()) {
      return false;
    }
  }
  auto channel = grpc::CreateChannel(
      base::StringPrintf("%s:%u", container_ip.c_str(), vm_tools::kGarconPort),
      grpc::InsecureChannelCredentials());
  container_name_to_garcon_channel_[container_name] = channel;
  container_name_to_garcon_stub_[container_name] =
      std::make_unique<vm_tools::container::Garcon::Stub>(channel);
  return true;
}

bool VirtualMachine::UnregisterContainer(const std::string& container_token) {
  auto token_iter = container_token_to_name_.find(container_token);
  if (token_iter == container_token_to_name_.end()) {
    return false;
  }
  auto name_iter = container_name_to_garcon_stub_.find(token_iter->second);
  DCHECK(name_iter != container_name_to_garcon_stub_.end());
  auto chan_iter = container_name_to_garcon_channel_.find(token_iter->second);
  DCHECK(chan_iter != container_name_to_garcon_channel_.end());
  container_token_to_name_.erase(token_iter);
  container_name_to_garcon_stub_.erase(name_iter);
  container_name_to_garcon_channel_.erase(chan_iter);
  return true;
}

std::string VirtualMachine::GenerateContainerToken(
    const std::string& container_name) {
  std::string token = base::GenerateGUID();
  pending_container_token_to_name_[token] = container_name;
  return token;
}

std::string VirtualMachine::GetContainerNameForToken(
    const std::string& container_token) {
  auto iter = container_token_to_name_.find(container_token);
  if (iter == container_token_to_name_.end()) {
    return "";
  }
  return iter->second;
}

bool VirtualMachine::LaunchContainerApplication(
    const std::string& container_name,
    const std::string& desktop_file_id,
    std::string* out_error) {
  CHECK(out_error);
  // Get the gRPC stub for communicating with the container.
  auto iter = container_name_to_garcon_stub_.find(container_name);
  if (iter == container_name_to_garcon_stub_.end() || !iter->second) {
    LOG(ERROR) << "Requested container " << container_name
               << " is not registered with the corresponding VM";
    out_error->assign("Requested container is not registered");
    return false;
  }

  vm_tools::container::LaunchApplicationRequest container_request;
  vm_tools::container::LaunchApplicationResponse container_response;
  container_request.set_desktop_file_id(desktop_file_id);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = iter->second->LaunchApplication(&ctx, container_request,
                                                        &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to launch application " << desktop_file_id
               << " in container " << container_name << ": "
               << status.error_message();
    out_error->assign("gRPC failure launching application: " +
                      status.error_message());
    return false;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.success();
}

bool VirtualMachine::LaunchVshd(const std::string& container_name,
                                uint32_t port,
                                std::string* out_error) {
  // Get the gRPC stub for communicating with the container.
  auto iter = container_name_to_garcon_stub_.find(container_name);
  if (iter == container_name_to_garcon_stub_.end() || !iter->second) {
    LOG(ERROR) << "Requested container " << container_name
               << " is not registered with the corresponding VM";
    out_error->assign("Requested container is not registered");
    return false;
  }

  vm_tools::container::LaunchVshdRequest container_request;
  vm_tools::container::LaunchVshdResponse container_response;
  container_request.set_port(port);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      iter->second->LaunchVshd(&ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to launch vshd in container " << container_name
               << ": " << status.error_message()
               << " code: " << status.error_code();
    out_error->assign("gRPC failure launching vshd in container: " +
                      status.error_message());
    return false;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.success();
}

bool VirtualMachine::GetContainerAppIcon(
    const std::string& container_name,
    std::vector<std::string> desktop_file_ids,
    uint32_t icon_size,
    uint32_t scale,
    std::vector<Icon>* icons) {
  CHECK(icons);

  // Get the gRPC stub for communicating with the container.
  auto iter = container_name_to_garcon_stub_.find(container_name);
  if (iter == container_name_to_garcon_stub_.end()) {
    LOG(ERROR) << "Requested container " << container_name
               << " is not registered with the corresponding VM";
    return false;
  }

  vm_tools::container::IconRequest container_request;
  vm_tools::container::IconResponse container_response;

  google::protobuf::RepeatedPtrField<std::string> ids(
      std::make_move_iterator(desktop_file_ids.begin()),
      std::make_move_iterator(desktop_file_ids.end()));
  container_request.mutable_desktop_file_ids()->Swap(&ids);
  container_request.set_icon_size(icon_size);
  container_request.set_scale(scale);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      iter->second->GetIcon(&ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get icons in container " << container_name << ": "
               << status.error_message();
    return false;
  }

  for (auto& icon : *container_response.mutable_desktop_icons()) {
    icons->emplace_back(
        Icon{.desktop_file_id = std::move(*icon.mutable_desktop_file_id()),
             .content = std::move(*icon.mutable_icon())});
  }
  return true;
}

bool VirtualMachine::IsContainerRunning(const std::string& container_name) {
  auto iter = container_name_to_garcon_channel_.find(container_name);
  if (iter == container_name_to_garcon_channel_.end() || !iter->second) {
    LOG(INFO) << "No such container: " << container_name;
    return false;
  }
  auto channel_state = iter->second->GetState(true);
  return channel_state == GRPC_CHANNEL_IDLE ||
         channel_state == GRPC_CHANNEL_CONNECTING ||
         channel_state == GRPC_CHANNEL_READY;
}

std::vector<std::string> VirtualMachine::GetContainerNames() {
  std::vector<std::string> retval;
  for (auto& container_entry : container_name_to_garcon_stub_) {
    retval.emplace_back(container_entry.first);
  }
  return retval;
}

}  // namespace cicerone
}  // namespace vm_tools