// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_manager_dbus_adaptor.h"

#include <memory>

#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/dbus/mock_dbus_service_watcher.h"
#include "shill/dbus/mock_dbus_service_watcher_factory.h"
#include "shill/error.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"

using dbus::MockBus;
using dbus::Response;
using std::string;
using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;
using testing::WithArg;

namespace shill {

class ChromeosManagerDBusAdaptorTest : public Test {
 public:
  ChromeosManagerDBusAdaptorTest()
      : adaptor_bus_(new MockBus(dbus::Bus::Options())),
        proxy_bus_(new MockBus(dbus::Bus::Options())),
        metrics_(&dispatcher_),
        manager_(&control_interface_, &dispatcher_, &metrics_),
        manager_adaptor_(adaptor_bus_, proxy_bus_, &manager_) {}

  virtual ~ChromeosManagerDBusAdaptorTest() {}

  void SetUp() override {
    manager_adaptor_.dbus_service_watcher_factory_ =
        &dbus_service_watcher_factory_;
  }

  void TearDown() override {}

 protected:
  scoped_refptr<MockBus> adaptor_bus_;
  scoped_refptr<MockBus> proxy_bus_;
  MockControl control_interface_;
  MockEventDispatcher dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  MockDBusServiceWatcherFactory dbus_service_watcher_factory_;
  ChromeosManagerDBusAdaptor manager_adaptor_;
};

void SetErrorTypeSuccess(Error* error) { error->Populate(Error::kSuccess); }

void SetErrorTypeFailure(Error* error) {
  error->Populate(Error::kOperationFailed);
}

TEST_F(ChromeosManagerDBusAdaptorTest, ClaimInterface) {
  brillo::ErrorPtr error;
  string kDefaultClaimerName = "";
  string kNonDefaultClaimerName = "test_claimer";
  string kInterfaceName = "test_interface";
  std::unique_ptr<Response> message(Response::CreateEmpty());

  // Watcher for device claimer is not created when we fail to claim the device.
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeFailure)));
  EXPECT_CALL(dbus_service_watcher_factory_, CreateDBusServiceWatcher(_, _, _))
      .Times(0);
  manager_adaptor_.ClaimInterface(&error, message.get(), kNonDefaultClaimerName,
                                  kInterfaceName);
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());

  // Watcher for device claimer is not created when we succeed in claiming the
  // device from the default claimer.
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeSuccess)));
  EXPECT_CALL(dbus_service_watcher_factory_, CreateDBusServiceWatcher(_, _, _))
      .Times(0);
  manager_adaptor_.ClaimInterface(&error, message.get(), kDefaultClaimerName,
                                  kInterfaceName);
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());

  // Watcher for device claimer is created when we succeed in claiming the
  // device from a non-default claimer.
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeSuccess)));
  EXPECT_CALL(dbus_service_watcher_factory_, CreateDBusServiceWatcher(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<MockDBusServiceWatcher>())));
  manager_adaptor_.ClaimInterface(&error, message.get(), kNonDefaultClaimerName,
                                  kInterfaceName);
  EXPECT_NE(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
}

TEST_F(ChromeosManagerDBusAdaptorTest, ReleaseInterface) {
  brillo::ErrorPtr error;
  string kClaimerName = "test_claimer";
  string kInterfaceName = "test_interface";
  std::unique_ptr<Response> message(Response::CreateEmpty());

  // Setup watcher for device claimer.
  manager_adaptor_.watcher_for_device_claimer_.reset(
      new MockDBusServiceWatcher());

  // If the device claimer is not removed, do not reset the watcher for device
  // claimer.
  EXPECT_CALL(manager_, ReleaseDevice(_, kInterfaceName, _, _))
      .WillOnce(SetArgPointee<2>(false));
  manager_adaptor_.ReleaseInterface(&error, message.get(), kClaimerName,
                                  kInterfaceName);
  EXPECT_NE(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());

  // If the device claimer is removed, reset the watcher for device claimer.
  EXPECT_CALL(manager_, ReleaseDevice(_, kInterfaceName, _, _))
      .WillOnce(SetArgPointee<2>(true));
  manager_adaptor_.ReleaseInterface(&error, message.get(), kClaimerName,
                                    kInterfaceName);
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
}

TEST_F(ChromeosManagerDBusAdaptorTest, SetupApModeInterface) {
  brillo::ErrorPtr error;
  string out_interface_name;
  std::unique_ptr<Response> message(Response::CreateEmpty());

  EXPECT_CALL(dbus_service_watcher_factory_, CreateDBusServiceWatcher(_, _, _))
      .Times(0);
  EXPECT_FALSE(manager_adaptor_.SetupApModeInterface(&error, message.get(),
                                                     &out_interface_name));
}

TEST_F(ChromeosManagerDBusAdaptorTest, SetupStationModeInterface) {
  brillo::ErrorPtr error;
  string out_interface_name;

  EXPECT_FALSE(
      manager_adaptor_.SetupStationModeInterface(&error, &out_interface_name));
}

TEST_F(ChromeosManagerDBusAdaptorTest, OnApModeSetterVanished) {
  // Setup watcher for AP mode setter.
  manager_adaptor_.watcher_for_ap_mode_setter_.reset(
      new MockDBusServiceWatcher());

  // Reset watcher for AP mode setter after AP mode setter vanishes.
  manager_adaptor_.OnApModeSetterVanished();
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_ap_mode_setter_.get());
}

TEST_F(ChromeosManagerDBusAdaptorTest, OnDeviceClaimerVanished) {
  // Setup watcher for device claimer.
  manager_adaptor_.watcher_for_device_claimer_.reset(
      new MockDBusServiceWatcher());

  // Reset watcher for device claimer after the device claimer vanishes.
  EXPECT_CALL(manager_, OnDeviceClaimerVanished());
  manager_adaptor_.OnDeviceClaimerVanished();
  EXPECT_EQ(nullptr, manager_adaptor_.watcher_for_device_claimer_.get());
}

}  // namespace shill
