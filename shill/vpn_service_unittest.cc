// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn_service.h"

#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/nice_mock_control.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_store.h"
#include "shill/mock_vpn_driver.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace shill {

class VPNServiceTest : public testing::Test {
 public:
  VPNServiceTest()
      : driver_(new MockVPNDriver()),
        manager_(&control_, NULL, NULL, NULL),
        service_(new VPNService(&control_, NULL, &metrics_, &manager_,
                                driver_)) {}

  virtual ~VPNServiceTest() {}

 protected:
  MockVPNDriver *driver_;  // Owned by |service_|.
  NiceMockControl control_;
  MockManager manager_;
  MockMetrics metrics_;
  VPNServiceRefPtr service_;
};

TEST_F(VPNServiceTest, TechnologyIs) {
  EXPECT_TRUE(service_->TechnologyIs(Technology::kVPN));
  EXPECT_FALSE(service_->TechnologyIs(Technology::kEthernet));
}

TEST_F(VPNServiceTest, Connect) {
  EXPECT_TRUE(service_->connectable());
  Error error;
  EXPECT_CALL(*driver_, Connect(_, &error));
  service_->Connect(&error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, ConnectAlreadyConnected) {
  Error error;
  EXPECT_CALL(*driver_, Connect(_, _)).Times(0);
  service_->state_ = Service::kStateOnline;
  service_->Connect(&error);
  EXPECT_EQ(Error::kAlreadyConnected, error.type());
  error.Reset();
  service_->state_ = Service::kStateConfiguring;
  service_->Connect(&error);
  EXPECT_EQ(Error::kAlreadyConnected, error.type());
}

TEST_F(VPNServiceTest, Disconnect) {
  Error error;
  EXPECT_CALL(*driver_, Disconnect());
  service_->Disconnect(&error);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoHost) {
  KeyValueStore args;
  Error error;
  args.SetString(flimflam::kProviderNameProperty, "vpn-name");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kInvalidProperty, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoName) {
  KeyValueStore args;
  Error error;
  args.SetString(flimflam::kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifier) {
  KeyValueStore args;
  Error error;
  args.SetString(flimflam::kProviderNameProperty, "vpn-name");
  args.SetString(flimflam::kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("vpn_10_8_0_1_vpn_name",
            VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, GetStorageIdentifier) {
  EXPECT_EQ("", service_->GetStorageIdentifier());
  service_->set_storage_id("foo");
  EXPECT_EQ("foo", service_->GetStorageIdentifier());
}

TEST_F(VPNServiceTest, GetDeviceRpcId) {
  Error error;
  EXPECT_EQ("/", service_->GetDeviceRpcId(&error));
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(VPNServiceTest, Load) {
  NiceMock<MockStore> storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  EXPECT_CALL(storage, ContainsGroup(kStorageID)).WillOnce(Return(true));
  EXPECT_CALL(*driver_, Load(&storage, kStorageID))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Load(&storage));
}

TEST_F(VPNServiceTest, Save) {
  NiceMock<MockStore> storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(VPNServiceTest, SaveCredentials) {
  NiceMock<MockStore> storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  service_->set_save_credentials(true);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, true))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(VPNServiceTest, Unload) {
  service_->set_auto_connect(true);
  service_->set_save_credentials(true);
  EXPECT_CALL(*driver_, Disconnect());
  EXPECT_CALL(*driver_, UnloadCredentials());
  manager_.vpn_provider()->services_.push_back(service_);
  service_->Unload();
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(service_->save_credentials());
  EXPECT_TRUE(manager_.vpn_provider()->services_.empty());
}

TEST_F(VPNServiceTest, InitPropertyStore) {
  EXPECT_CALL(*driver_, InitPropertyStore(service_->mutable_store()));
  service_->InitDriverPropertyStore();
}

}  // namespace shill
