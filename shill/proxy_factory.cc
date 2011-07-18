// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/proxy_factory.h"

#include "shill/modem_manager_proxy.h"
#include "shill/supplicant_interface_proxy.h"
#include "shill/supplicant_process_proxy.h"

namespace shill {

ProxyFactory *ProxyFactory::factory_ = NULL;

ProxyFactory::ProxyFactory() {}

ProxyFactory::~ProxyFactory() {}

ModemManagerProxyInterface *ProxyFactory::CreateModemManagerProxy(
    ModemManager *manager,
    const std::string &path,
    const std::string &service) {
  return new ModemManagerProxy(manager, path, service);
}

SupplicantProcessProxyInterface *ProxyFactory::CreateProcessProxy(
    const char *dbus_path,
    const char *dbus_addr) {
  return new SupplicantProcessProxy(dbus_path, dbus_addr);
}

SupplicantInterfaceProxyInterface *ProxyFactory::CreateInterfaceProxy(
    const WiFiRefPtr &wifi,
    const DBus::Path &object_path,
    const char *dbus_addr) {
  return new SupplicantInterfaceProxy(wifi, object_path, dbus_addr);
}

}  // namespace shill
