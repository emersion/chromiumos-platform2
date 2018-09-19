// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_constants.h"

namespace oobe_config {

const base::FilePath kStatefulPartition =
    base::FilePath("/mnt/stateful_partition/");

const base::FilePath kSaveTempPath =
    base::FilePath("/var/lib/oobe_config_save/");
const base::FilePath kRestoreTempPath =
    base::FilePath("/var/lib/oobe_config_restore/");
const base::FilePath kRollbackDataPath = base::FilePath(
    "/mnt/stateful_partition/unencrypted/preserve/rollback_data");

const base::FilePath kInstallAttributesPath =
    base::FilePath("/home/.shadow/install_attributes.pb");
const char kInstallAttributesFileName[] = "install_attributes.pb";

const base::FilePath kOwnerKeyFilePath =
    base::FilePath("/var/lib/whitelist/owner.key");
const char kOwnerKeyFileName[] = "owner.key";

const base::FilePath kPolicyFileDirectory =
    base::FilePath("/var/lib/whitelist/");
const char kPolicyFileName[] = "policy";
const char kPolicyFileNamePattern[] = "policy*";

const base::FilePath kShillDefaultProfilePath =
    base::FilePath("/var/cache/shill/default.profile");
const char kShillDefaultProfileFileName[] = "default.profile";

const char kPolicyDotOneFileNameForTesting[] = "policy.1";

const char kOobeConfigSaveUsername[] = "oobe_config_save";
const char kRootUsername[] = "root";

}  // namespace oobe_config