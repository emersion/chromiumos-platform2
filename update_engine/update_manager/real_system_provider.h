// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_

#include <base/memory/scoped_ptr.h>

#include <string>

#include "update_engine/hardware_interface.h"
#include "update_engine/update_manager/system_provider.h"

namespace chromeos_update_manager {

// SystemProvider concrete implementation.
class RealSystemProvider : public SystemProvider {
 public:
  explicit RealSystemProvider(
      chromeos_update_engine::HardwareInterface* hardware)
      : hardware_(hardware) {}

  // Initializes the provider and returns whether it succeeded.
  bool Init();

  virtual Variable<bool>* var_is_normal_boot_mode() override {
    return var_is_normal_boot_mode_.get();
  }

  virtual Variable<bool>* var_is_official_build() override {
    return var_is_official_build_.get();
  }

  virtual Variable<bool>* var_is_oobe_complete() override {
    return var_is_oobe_complete_.get();
  }

 private:
  scoped_ptr<Variable<bool>> var_is_normal_boot_mode_;
  scoped_ptr<Variable<bool>> var_is_official_build_;
  scoped_ptr<Variable<bool>> var_is_oobe_complete_;

  chromeos_update_engine::HardwareInterface* hardware_;

  DISALLOW_COPY_AND_ASSIGN(RealSystemProvider);
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_SYSTEM_PROVIDER_H_
