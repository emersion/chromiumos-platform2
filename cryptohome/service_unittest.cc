// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for Service

#include "service.h"

#include <base/at_exit.h>
#include <base/threading/platform_thread.h>
#include <base/file_util.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "crypto.h"
#include "make_tests.h"
#include "mock_homedirs.h"
#include "mock_install_attributes.h"
#include "mock_mount.h"
#include "mock_tpm.h"
#include "secure_blob.h"
#include "username_passkey.h"

using base::PlatformThread;

namespace cryptohome {
using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

const char kImageDir[] = "test_image_dir";
const char kSkelDir[] = "test_image_dir/skel";
class ServiceInterfaceTest : public ::testing::Test {
 public:
  ServiceInterfaceTest() { }
  virtual ~ServiceInterfaceTest() { }

  void SetUp() {
    FilePath image_dir(kImageDir);
    FilePath path = image_dir.Append("salt");
    ASSERT_TRUE(file_util::PathExists(path)) << path.value()
                                             << " does not exist!";

    int64 file_size;
    ASSERT_TRUE(file_util::GetFileSize(path, &file_size))
                << "Could not get size of "
                << path.value();

    char* buf = new char[file_size];
    int data_read = file_util::ReadFile(path, buf, file_size);
    system_salt_.assign(buf, buf + data_read);
    delete[] buf;
  }

 protected:
  // Protected for trivial access
  chromeos::Blob system_salt_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceInterfaceTest);
};

class ServiceSubclass : public Service {
 public:
  ServiceSubclass()
    : Service(),
      completed_tasks_() { }
  virtual ~ServiceSubclass() { }

  virtual void NotifyEvent(CryptohomeEventBase* result) {
    if (strcmp(result->GetEventName(), kMountTaskResultEventType))
      return;
    MountTaskResult* r = static_cast<MountTaskResult*>(result);
    completed_tasks_.push_back(*r);
  }

  virtual void Dispatch() {
    DispatchEvents();
  }

  std::vector<MountTaskResult> completed_tasks_;
};

TEST_F(ServiceInterfaceTest, CheckKeySuccessTest) {
  MockMount mount;
  MockHomeDirs homedirs;
  EXPECT_CALL(homedirs, Init())
      .WillOnce(Return(true));
  EXPECT_CALL(mount, Init())
      .WillOnce(Return(true));
  EXPECT_CALL(mount, TestCredentials(_))
      .WillOnce(Return(true));

  Service service;
  service.set_homedirs(&homedirs);
  service.set_mount_for_user("", &mount);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  service.Initialize();
  gboolean out = FALSE;
  GError *error = NULL;

  char user[] = "chromeos-user";
  char key[] = "274146c6e8886a843ddfea373e2dc71b";
  EXPECT_TRUE(service.CheckKey(user, key, &out, &error));
  EXPECT_TRUE(out);
}

TEST_F(ServiceInterfaceTest, CheckAsyncTestCredentials) {
  Mount mount;
  NiceMock<MockTpm> tpm;
  mount.get_crypto()->set_tpm(&tpm);
  mount.set_shadow_root(kImageDir);
  mount.set_skel_source(kSkelDir);
  mount.set_use_tpm(false);
  mount.set_policy_provider(new policy::PolicyProvider(
      new NiceMock<policy::MockDevicePolicy>));

  HomeDirs homedirs;
  homedirs.crypto()->set_tpm(&tpm);
  homedirs.set_shadow_root(kImageDir);
  homedirs.crypto()->set_use_tpm(false);
  homedirs.set_policy_provider(new policy::PolicyProvider(
      new NiceMock<policy::MockDevicePolicy>));

  ServiceSubclass service;
  service.set_homedirs(&homedirs);
  service.set_mount_for_user("", &mount);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  service.Initialize();

  cryptohome::SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(kDefaultUsers[7].password,
                                        system_salt_, &passkey);
  std::string passkey_string(static_cast<const char*>(passkey.const_data()),
                             passkey.size());

  gboolean out = FALSE;
  GError *error = NULL;
  gint async_id = -1;
  EXPECT_TRUE(service.AsyncCheckKey(
      const_cast<gchar*>(static_cast<const gchar*>(kDefaultUsers[7].username)),
      const_cast<gchar*>(static_cast<const gchar*>(passkey_string.c_str())),
      &async_id,
      &error));
  EXPECT_NE(-1, async_id);
  for (unsigned int i = 0; i < 64; i++) {
    bool found = false;
    service.Dispatch();
    for (unsigned int j = 0; j < service.completed_tasks_.size(); j++) {
      if (service.completed_tasks_[j].sequence_id() == async_id) {
        out = service.completed_tasks_[j].return_status();
        found = true;
      }
    }
    if (found) {
      break;
    }
    PlatformThread::Sleep(100);
  }
  EXPECT_TRUE(out);
}

TEST(Standalone, CheckAutoCleanupCallback) {
  // Checks that AutoCleanupCallback() is called periodically.
  NiceMock<MockMount> mount;
  MockHomeDirs homedirs;
  Service service;
  service.set_mount_for_user("", &mount);
  service.set_homedirs(&homedirs);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);

  // Service will schedule periodic clean-ups. Wait a bit and make
  // sure that we had at least 3 executed.
  EXPECT_CALL(homedirs, Init())
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs, FreeDiskSpace())
      .Times(::testing::AtLeast(3));
  EXPECT_CALL(mount, UpdateCurrentUserActivityTimestamp(0))
      .Times(::testing::AtLeast(3));
  service.set_auto_cleanup_period(2);  // 2ms = 500HZ
  service.set_update_user_activity_period(2);  // 2 x 5ms = 25HZ
  service.Initialize();
  PlatformThread::Sleep(100);
}

}  // namespace cryptohome
