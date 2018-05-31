// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_SERVICE_H_
#define VM_TOOLS_CICERONE_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <grpc++/grpc++.h>

#include "vm_tools/cicerone/container_listener_impl.h"
#include "vm_tools/cicerone/virtual_machine.h"

namespace vm_tools {
namespace cicerone {

// VM Container Service responsible for responding to DBus method calls for
// interacting with VM containers.
class Service final : public base::MessageLoopForIO::Watcher {
 public:
  // Creates a new Service instance.  |quit_closure| is posted to the TaskRunner
  // for the current thread when this process receives a SIGTERM.
  static std::unique_ptr<Service> Create(base::Closure quit_closure);
  ~Service() override;

  // base::MessageLoopForIO::Watcher overrides.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  // Notifies the service that a container with |container_token| and IP of
  // |container_ip| has completed startup. Sets |result| to true if this maps to
  // a subnet inside a currently running VM and |container_token| matches a
  // security token for that VM; false otherwise. Signals |event| when done.
  void ContainerStartupCompleted(const std::string& container_token,
                                 const uint32_t container_ip,
                                 bool* result,
                                 base::WaitableEvent* event);

  // Notifies the service that a container with |container_token| and IP of
  // |container_ip| is shutting down. Sets |result| to true if this maps to
  // a subnet inside a currently running VM and |container_token| matches a
  // security token for that VM; false otherwise. Signals |event| when done.
  void ContainerShutdown(const std::string& container_token,
                         const uint32_t container_ip,
                         bool* result,
                         base::WaitableEvent* event);

  // This will send a D-Bus message to Chrome to inform it of the current
  // installed application list for a container. It will use |container_ip| to
  // resolve the request to a VM and then |container_token| to resolve it to a
  // container. |app_list| should be populated with the list of installed
  // applications but the vm & container names should be left blank; it must
  // remain valid for the lifetime of this call. |result| is set to true on
  // success, false otherwise. Signals |event| when done.
  void UpdateApplicationList(const std::string& container_token,
                             const uint32_t container_ip,
                             vm_tools::apps::ApplicationList* app_list,
                             bool* result,
                             base::WaitableEvent* event);

  // Sends a D-Bus message to Chrome to tell it to open the |url| in a new tab.
  // |container_ip| should be the IP address of the container the request is
  // coming form. |result| is set to true on success, false otherwise. Signals
  // |event| when done.
  void OpenUrl(const std::string& url,
               uint32_t container_ip,
               bool* result,
               base::WaitableEvent* event);

 private:
  explicit Service(base::Closure quit_closure);

  // Initializes the service by connecting to the system DBus daemon, exporting
  // its methods, and taking ownership of it's name.
  bool Init();

  // Handles a SIGTERM.
  void HandleSigterm();

  // Handles notification a VM is starting.
  std::unique_ptr<dbus::Response> NotifyVmStarted(
      dbus::MethodCall* method_call);

  // Handles a notification a VM is stopping.
  std::unique_ptr<dbus::Response> NotifyVmStopped(
      dbus::MethodCall* method_call);

  // Handles a request to get a security token to associate with a container.
  std::unique_ptr<dbus::Response> GetContainerToken(
      dbus::MethodCall* method_call);

  // Handles a request to check if a container is currently running.
  std::unique_ptr<dbus::Response> IsContainerRunning(
      dbus::MethodCall* method_call);

  // Handles a request to launch an application in a container.
  std::unique_ptr<dbus::Response> LaunchContainerApplication(
      dbus::MethodCall* method_call);

  // Handles a request to get application icons in a container.
  std::unique_ptr<dbus::Response> GetContainerAppIcon(
      dbus::MethodCall* method_call);

  // Handles a request to launch vshd in a container.
  std::unique_ptr<dbus::Response> LaunchVshd(dbus::MethodCall* method_call);

  // Gets the VirtualMachine that corresponds to a container at |container_ip|
  // and sets |vm_out| to the VirtualMachine, |owner_id_out| to the owner id of
  // the VM, and |name_out| to the name of the VM. Returns false if no such
  // mapping exists.
  bool GetVirtualMachineForContainerIp(uint32_t container_ip,
                                       VirtualMachine** vm_out,
                                       std::string* owner_id_out,
                                       std::string* name_out);

  // Registers |hostname| and |ip| with the hostname resolver service so that
  // the container is reachable from a known hostname.
  void RegisterHostname(const std::string& hostname, const std::string& ip);

  // Unregisters containers associated with this |vm| with |owner_id| and
  // |vm_name|.  All hostnames are removed from the hostname resolver service,
  // and the ContainerShutdown signal is sent via D-Bus.
  void UnregisterVmContainers(VirtualMachine* vm,
                              const std::string& owner_id,
                              const std::string& vm_name);

  // Unregisters |hostname| with the hostname resolver service.
  void UnregisterHostname(const std::string& hostname);

  // Callback for when the crosdns D-Bus service goes online (or is online
  // already) so we can then register the NameOnwerChanged callback.
  void OnCrosDnsServiceAvailable(bool service_is_available);

  // Callback for when the crosdns D-Bus service restarts so we can
  // re-register any of our hostnames that are active.
  void OnCrosDnsNameOwnerChanged(const std::string& old_owner,
                                 const std::string& new_owner);

  // Gets a VirtualMachine pointer to the registered VM with corresponding
  // |owner_id| and |vm_name|. Returns a nullptr if not found.
  VirtualMachine* FindVm(const std::string& owner_id,
                         const std::string& vm_name);

  // File descriptor for the SIGTERM event.
  base::ScopedFD signal_fd_;
  base::MessageLoopForIO::FileDescriptorWatcher watcher_;

  // Key for VMs in the map, which is the owner ID and VM name as a pair.
  using VmKey = std::pair<std::string, std::string>;
  // Running VMs.
  std::map<VmKey, std::unique_ptr<VirtualMachine>> vms_;

  // Connection to the system bus.
  scoped_refptr<dbus::Bus> bus_;
  dbus::ExportedObject* exported_object_;             // Owned by |bus_|.
  dbus::ObjectProxy* vm_applications_service_proxy_;  // Owned by |bus_|.
  dbus::ObjectProxy* url_handler_service_proxy_;      // Owned by |bus_|.
  dbus::ObjectProxy* crosdns_service_proxy_;          // Owned by |bus_|.

  // The ContainerListener service.
  std::unique_ptr<ContainerListenerImpl> container_listener_;

  // Thread on which the ContainerListener service lives.
  base::Thread grpc_thread_container_{"gRPC Container Server Thread"};

  // The server where the ContainerListener service lives.
  std::shared_ptr<grpc::Server> grpc_server_container_;

  // Closure that's posted to the current thread's TaskRunner when the service
  // receives a SIGTERM.
  base::Closure quit_closure_;

  // Ensure calls are made on the right thread.
  base::SequenceChecker sequence_checker_;

  // Map of hostnames/IPs we have registered so we can re-register them if the
  // resolver service restarts.
  std::map<std::string, std::string> hostname_mappings_;

  // Owner of the primary VM, we only do hostname mappings for the primary VM.
  std::string primary_owner_id_;

  base::WeakPtrFactory<Service> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_SERVICE_H_