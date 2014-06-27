// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_CONFIG_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_CONFIG_PROVIDER_H_

#include "update_engine/update_manager/provider.h"
#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// Provider for const system configurations. This provider reads the
// configuration from a file on /etc.
class ConfigProvider : public Provider {
 public:
  // Returns a variable stating whether the out of the box experience (OOBE) is
  // enabled on this device. A value of false means that the device doesn't have
  // an OOBE workflow.
  virtual Variable<bool>* var_is_oobe_enabled() = 0;

 protected:
  ConfigProvider() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ConfigProvider);
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_CONFIG_PROVIDER_H_
