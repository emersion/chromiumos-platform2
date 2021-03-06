// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_rpc_task_dbus_adaptor.h"

#include "shill/error.h"
#include "shill/logging.h"
#include "shill/rpc_task.h"

using std::map;
using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(ChromeosRPCTaskDBusAdaptor* r) {
  return r->GetRpcIdentifier();
}
}

// static
const char ChromeosRPCTaskDBusAdaptor::kPath[] = "/task/";

ChromeosRPCTaskDBusAdaptor::ChromeosRPCTaskDBusAdaptor(
    const scoped_refptr<dbus::Bus>& bus,
    RPCTask* task)
    : org::chromium::flimflam::TaskAdaptor(this),
      ChromeosDBusAdaptor(bus, kPath + task->UniqueName()),
      task_(task),
      connection_name_(bus->GetConnectionName()) {
  // Register DBus object.
  RegisterWithDBusObject(dbus_object());
  dbus_object()->RegisterAndBlock();
}

ChromeosRPCTaskDBusAdaptor::~ChromeosRPCTaskDBusAdaptor() {
  dbus_object()->UnregisterAsync();
  task_ = nullptr;
}

const string& ChromeosRPCTaskDBusAdaptor::GetRpcIdentifier() const {
  return dbus_path().value();
}

const string& ChromeosRPCTaskDBusAdaptor::GetRpcConnectionIdentifier() const {
  return connection_name_;
}

bool ChromeosRPCTaskDBusAdaptor::getsec(
    brillo::ErrorPtr* /*error*/, string* user, string* password) {
  SLOG(this, 2) << __func__ << ": " << user;
  task_->GetLogin(user, password);
  return true;
}

bool ChromeosRPCTaskDBusAdaptor::notify(brillo::ErrorPtr* /*error*/,
                                        const string& reason,
                                        const map<string, string>& dict) {
  SLOG(this, 2) << __func__ << ": " << reason;
  task_->Notify(reason, dict);
  return true;
}

}  // namespace shill
