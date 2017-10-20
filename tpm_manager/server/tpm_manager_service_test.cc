//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <base/at_exit.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tpm_manager/server/mock_local_data_store.h"
#include "tpm_manager/server/mock_tpm_initializer.h"
#include "tpm_manager/server/mock_tpm_nvram.h"
#include "tpm_manager/server/mock_tpm_status.h"
#include "tpm_manager/server/tpm_manager_service.h"

using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::_;

namespace {

const char kOwnerPassword[] = "owner";
const char kOwnerDependency[] = "owner_dependency";
const char kOtherDependency[] = "other_dependency";

}  // namespace

namespace tpm_manager {

// A test fixture that takes care of message loop management and configuring a
// TpmManagerService instance with mock dependencies.
class TpmManagerServiceTest : public testing::Test {
 public:
  ~TpmManagerServiceTest() override = default;
  void SetUp() override {
    service_.reset(new TpmManagerService(
        true /*wait_for_ownership*/, &mock_local_data_store_, &mock_tpm_status_,
        &mock_tpm_initializer_, &mock_tpm_nvram_));
    SetupService();
  }

 protected:
  void Run() { run_loop_.Run(); }

  void RunServiceWorkerAndQuit() {
    // Run out the service worker loop by posting a new command and waiting for
    // the response.
    auto callback = [this](const GetTpmStatusReply& reply) { Quit(); };
    GetTpmStatusRequest request;
    service_->GetTpmStatus(request, base::Bind(callback));
    Run();
  }

  void Quit() { run_loop_.Quit(); }

  void SetupService() { CHECK(service_->Initialize()); }

  NiceMock<MockLocalDataStore> mock_local_data_store_;
  NiceMock<MockTpmInitializer> mock_tpm_initializer_;
  NiceMock<MockTpmNvram> mock_tpm_nvram_;
  NiceMock<MockTpmStatus> mock_tpm_status_;
  std::unique_ptr<TpmManagerService> service_;

 private:
  base::MessageLoop message_loop_;
  base::RunLoop run_loop_;
};

// Tests must call SetupService().
class TpmManagerServiceTest_NoWaitForOwnership : public TpmManagerServiceTest {
 public:
  ~TpmManagerServiceTest_NoWaitForOwnership() override = default;
  void SetUp() override {
    service_.reset(new TpmManagerService(
        false /*wait_for_ownership*/, &mock_local_data_store_,
        &mock_tpm_status_, &mock_tpm_initializer_, &mock_tpm_nvram_));
  }
};

TEST_F(TpmManagerServiceTest_NoWaitForOwnership, AutoInitialize) {
  // Make sure InitializeTpm doesn't get multiple calls.
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm()).Times(1);
  EXPECT_CALL(mock_tpm_initializer_, PreInitializeTpm()).Times(0);
  SetupService();
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest_NoWaitForOwnership, AutoInitializeNoTpm) {
  EXPECT_CALL(mock_tpm_status_, IsTpmEnabled()).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm()).Times(0);
  EXPECT_CALL(mock_tpm_initializer_, PreInitializeTpm()).Times(0);
  SetupService();
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest_NoWaitForOwnership, AutoInitializeFailure) {
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm())
      .WillRepeatedly(Return(false));
  SetupService();
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest_NoWaitForOwnership,
       TakeOwnershipAfterAutoInitialize) {
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm()).Times(AtLeast(2));
  SetupService();
  auto callback = [this](const TakeOwnershipReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    Quit();
  };
  TakeOwnershipRequest request;
  service_->TakeOwnership(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, NoAutoInitialize) {
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm()).Times(0);
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, GetTpmStatusSuccess) {
  EXPECT_CALL(mock_tpm_status_, GetDictionaryAttackInfo(_, _, _, _))
      .WillRepeatedly(Invoke([](int* counter, int* threshold, bool* lockout,
                                int* seconds_remaining) {
        *counter = 5;
        *threshold = 6;
        *lockout = true;
        *seconds_remaining = 7;
        return true;
      }));
  EXPECT_CALL(mock_tpm_status_, GetVersionInfo(_, _, _, _, _, _))
      .WillRepeatedly(Invoke([](uint32_t* family, uint64_t* spec_level,
                                uint32_t* manufacturer, uint32_t* tpm_model,
                                uint64_t* firmware_version,
                                std::vector<uint8_t>* vendor_specific) {
        *family = 8;
        *spec_level = 9;
        *manufacturer = 10;
        *tpm_model = 11;
        *firmware_version = 12;
        *vendor_specific = { 0xda, 0x7a };
        return true;
      }));
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(local_data), Return(true)));

  auto callback = [this](const GetTpmStatusReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_TRUE(reply.enabled());
    EXPECT_TRUE(reply.owned());
    EXPECT_EQ(kOwnerPassword, reply.local_data().owner_password());
    EXPECT_EQ(5, reply.dictionary_attack_counter());
    EXPECT_EQ(6, reply.dictionary_attack_threshold());
    EXPECT_TRUE(reply.dictionary_attack_lockout_in_effect());
    EXPECT_EQ(7, reply.dictionary_attack_lockout_seconds_remaining());
    EXPECT_EQ(8, reply.version_info().family());
    EXPECT_EQ(9, reply.version_info().spec_level());
    EXPECT_EQ(10, reply.version_info().manufacturer());
    EXPECT_EQ(11, reply.version_info().tpm_model());
    EXPECT_EQ(12, reply.version_info().firmware_version());
    EXPECT_EQ("\xda\x7a", reply.version_info().vendor_specific());
    Quit();
  };
  GetTpmStatusRequest request;
  service_->GetTpmStatus(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, GetTpmStatusLocalDataFailure) {
  EXPECT_CALL(mock_local_data_store_, Read(_)).WillRepeatedly(Return(false));
  auto callback = [this](const GetTpmStatusReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_TRUE(reply.enabled());
    EXPECT_TRUE(reply.owned());
    EXPECT_FALSE(reply.has_local_data());
    EXPECT_TRUE(reply.has_dictionary_attack_counter());
    EXPECT_TRUE(reply.has_dictionary_attack_threshold());
    EXPECT_TRUE(reply.has_dictionary_attack_lockout_in_effect());
    EXPECT_TRUE(reply.has_dictionary_attack_lockout_seconds_remaining());
    Quit();
  };
  GetTpmStatusRequest request;
  service_->GetTpmStatus(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, GetTpmStatusNoTpm) {
  EXPECT_CALL(mock_tpm_status_, IsTpmEnabled()).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_status_, GetDictionaryAttackInfo(_, _, _, _))
      .WillRepeatedly(Return(false));
  auto callback = [this](const GetTpmStatusReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.enabled());
    EXPECT_TRUE(reply.owned());
    EXPECT_TRUE(reply.has_local_data());
    EXPECT_FALSE(reply.has_dictionary_attack_counter());
    EXPECT_FALSE(reply.has_dictionary_attack_threshold());
    EXPECT_FALSE(reply.has_dictionary_attack_lockout_in_effect());
    EXPECT_FALSE(reply.has_dictionary_attack_lockout_seconds_remaining());
    Quit();
  };
  GetTpmStatusRequest request;
  service_->GetTpmStatus(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, TakeOwnershipSuccess) {
  // Make sure InitializeTpm doesn't get multiple calls.
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm()).Times(1);
  auto callback = [this](const TakeOwnershipReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    Quit();
  };
  TakeOwnershipRequest request;
  service_->TakeOwnership(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, TakeOwnershipFailure) {
  EXPECT_CALL(mock_tpm_initializer_, InitializeTpm())
      .WillRepeatedly(Return(false));
  auto callback = [this](const TakeOwnershipReply& reply) {
    EXPECT_EQ(STATUS_DEVICE_ERROR, reply.status());
    Quit();
  };
  TakeOwnershipRequest request;
  service_->TakeOwnership(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, TakeOwnershipNoTpm) {
  EXPECT_CALL(mock_tpm_status_, IsTpmEnabled()).WillRepeatedly(Return(false));
  auto callback = [this](const TakeOwnershipReply& reply) {
    EXPECT_EQ(STATUS_NOT_AVAILABLE, reply.status());
    Quit();
  };
  TakeOwnershipRequest request;
  service_->TakeOwnership(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, RemoveOwnerDependencyReadFailure) {
  EXPECT_CALL(mock_local_data_store_, Read(_)).WillRepeatedly(Return(false));
  auto callback = [this](const RemoveOwnerDependencyReply& reply) {
    EXPECT_EQ(STATUS_DEVICE_ERROR, reply.status());
    Quit();
  };
  RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(kOwnerDependency);
  service_->RemoveOwnerDependency(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, RemoveOwnerDependencyWriteFailure) {
  EXPECT_CALL(mock_local_data_store_, Write(_)).WillRepeatedly(Return(false));
  auto callback = [this](const RemoveOwnerDependencyReply& reply) {
    EXPECT_EQ(STATUS_DEVICE_ERROR, reply.status());
    Quit();
  };
  RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(kOwnerDependency);
  service_->RemoveOwnerDependency(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, RemoveOwnerDependencyNotCleared) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  local_data.add_owner_dependency(kOwnerDependency);
  local_data.add_owner_dependency(kOtherDependency);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_))
      .WillOnce(DoAll(SaveArg<0>(&local_data), Return(true)));
  auto callback = [this, &local_data](const RemoveOwnerDependencyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(1, local_data.owner_dependency_size());
    EXPECT_EQ(kOtherDependency, local_data.owner_dependency(0));
    EXPECT_TRUE(local_data.has_owner_password());
    EXPECT_EQ(kOwnerPassword, local_data.owner_password());
    Quit();
  };
  RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(kOwnerDependency);
  service_->RemoveOwnerDependency(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, RemoveOwnerDependencyCleared) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  local_data.add_owner_dependency(kOwnerDependency);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_))
      .WillOnce(DoAll(SaveArg<0>(&local_data), Return(true)));
  auto callback = [this, &local_data](const RemoveOwnerDependencyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(0, local_data.owner_dependency_size());
    EXPECT_TRUE(local_data.has_owner_password());
    Quit();
  };
  RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(kOwnerDependency);
  service_->RemoveOwnerDependency(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, RemoveOwnerDependencyNotPresent) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  local_data.add_owner_dependency(kOwnerDependency);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_))
      .WillOnce(DoAll(SaveArg<0>(&local_data), Return(true)));
  auto callback = [this, &local_data](const RemoveOwnerDependencyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(1, local_data.owner_dependency_size());
    EXPECT_EQ(kOwnerDependency, local_data.owner_dependency(0));
    EXPECT_TRUE(local_data.has_owner_password());
    EXPECT_EQ(kOwnerPassword, local_data.owner_password());
    Quit();
  };
  RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(kOtherDependency);
  service_->RemoveOwnerDependency(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, ClearStoredOwnerPasswordReadFailure) {
  EXPECT_CALL(mock_local_data_store_, Read(_)).WillRepeatedly(Return(false));
  auto callback = [this](const ClearStoredOwnerPasswordReply& reply) {
    EXPECT_EQ(STATUS_DEVICE_ERROR, reply.status());
    Quit();
  };
  ClearStoredOwnerPasswordRequest request;
  service_->ClearStoredOwnerPassword(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, ClearStoredOwnerPasswordWriteFailure) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_)).WillRepeatedly(Return(false));
  auto callback = [this](const ClearStoredOwnerPasswordReply& reply) {
    EXPECT_EQ(STATUS_DEVICE_ERROR, reply.status());
    Quit();
  };
  ClearStoredOwnerPasswordRequest request;
  service_->ClearStoredOwnerPassword(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, ClearStoredOwnerPasswordRemainingDependencies) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  local_data.add_owner_dependency(kOwnerDependency);
  local_data.add_owner_dependency(kOtherDependency);
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_))
      .Times(0);
  auto callback =
      [this, &local_data](const ClearStoredOwnerPasswordReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_TRUE(local_data.has_owner_password());
    EXPECT_EQ(kOwnerPassword, local_data.owner_password());
    Quit();
  };
  ClearStoredOwnerPasswordRequest request;
  service_->ClearStoredOwnerPassword(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, ClearStoredOwnerPasswordNoDependencies) {
  LocalData local_data;
  local_data.set_owner_password(kOwnerPassword);
  local_data.set_endorsement_password("endorsement password");
  local_data.set_lockout_password("lockout password");
  EXPECT_CALL(mock_local_data_store_, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(local_data), Return(true)));
  EXPECT_CALL(mock_local_data_store_, Write(_))
      .WillOnce(DoAll(SaveArg<0>(&local_data), Return(true)));
  auto callback =
      [this, &local_data](const ClearStoredOwnerPasswordReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(local_data.has_owner_password());
    EXPECT_TRUE(local_data.has_endorsement_password());
    EXPECT_TRUE(local_data.has_lockout_password());
    Quit();
  };
  ClearStoredOwnerPasswordRequest request;
  service_->ClearStoredOwnerPassword(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, DefineSpaceFailure) {
  uint32_t nvram_index = 5;
  size_t nvram_size = 32;
  std::vector<NvramSpaceAttribute> attributes{NVRAM_BOOT_WRITE_LOCK};
  NvramSpacePolicy policy = NVRAM_POLICY_PCR0;
  std::string auth_value = "1234";
  EXPECT_CALL(mock_tpm_nvram_, DefineSpace(nvram_index, nvram_size, attributes,
                                           auth_value, policy))
      .WillRepeatedly(Return(NVRAM_RESULT_INVALID_PARAMETER));
  auto callback = [this](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_INVALID_PARAMETER, reply.result());
    Quit();
  };
  DefineSpaceRequest request;
  request.set_index(nvram_index);
  request.set_size(nvram_size);
  request.add_attributes(NVRAM_BOOT_WRITE_LOCK);
  request.set_policy(policy);
  request.set_authorization_value(auth_value);
  service_->DefineSpace(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, DefineSpaceSuccess) {
  uint32_t nvram_index = 5;
  uint32_t nvram_size = 32;
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto list_callback = [nvram_index](const ListSpacesReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_EQ(1, reply.index_list_size());
    EXPECT_EQ(nvram_index, reply.index_list(0));
  };
  auto info_callback = [nvram_size](const GetSpaceInfoReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_EQ(nvram_size, reply.size());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_size);
  service_->DefineSpace(define_request, base::Bind(define_callback));
  ListSpacesRequest list_request;
  service_->ListSpaces(list_request, base::Bind(list_callback));
  GetSpaceInfoRequest info_request;
  info_request.set_index(nvram_index);
  service_->GetSpaceInfo(info_request, base::Bind(info_callback));
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, DestroyUnitializedNvram) {
  auto callback = [this](const DestroySpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST, reply.result());
    Quit();
  };
  DestroySpaceRequest request;
  service_->DestroySpace(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, DestroySpaceSuccess) {
  uint32_t nvram_index = 5;
  uint32_t nvram_size = 32;
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto destroy_callback = [](const DestroySpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_size);
  service_->DefineSpace(define_request, base::Bind(define_callback));
  DestroySpaceRequest destroy_request;
  destroy_request.set_index(nvram_index);
  service_->DestroySpace(destroy_request, base::Bind(destroy_callback));
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, DoubleDestroySpace) {
  uint32_t nvram_index = 5;
  uint32_t nvram_size = 32;
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto destroy_callback_success = [](const DestroySpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto destroy_callback_failure = [](const DestroySpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST, reply.result());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_size);
  service_->DefineSpace(define_request, base::Bind(define_callback));
  DestroySpaceRequest destroy_request;
  destroy_request.set_index(nvram_index);
  service_->DestroySpace(destroy_request, base::Bind(destroy_callback_success));
  service_->DestroySpace(destroy_request, base::Bind(destroy_callback_failure));
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, WriteSpaceIncorrectSize) {
  uint32_t nvram_index = 5;
  std::string nvram_data("nvram_data");
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto write_callback = [](const WriteSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_INVALID_PARAMETER, reply.result());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_data.size() - 1);
  service_->DefineSpace(define_request, base::Bind(define_callback));
  WriteSpaceRequest write_request;
  write_request.set_index(nvram_index);
  write_request.set_data(nvram_data);
  service_->WriteSpace(write_request, base::Bind(write_callback));
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, WriteBeforeAfterLock) {
  uint32_t nvram_index = 5;
  std::string nvram_data("nvram_data");
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto write_callback_success = [](const WriteSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto lock_callback = [](const LockSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto write_callback_failure = [](const WriteSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_OPERATION_DISABLED, reply.result());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_data.size());
  service_->DefineSpace(define_request, base::Bind(define_callback));
  WriteSpaceRequest write_request;
  write_request.set_index(nvram_index);
  write_request.set_data(nvram_data);
  service_->WriteSpace(write_request, base::Bind(write_callback_success));
  LockSpaceRequest lock_request;
  lock_request.set_index(nvram_index);
  lock_request.set_lock_write(true);
  service_->LockSpace(lock_request, base::Bind(lock_callback));
  service_->WriteSpace(write_request, base::Bind(write_callback_failure));
  RunServiceWorkerAndQuit();
}

TEST_F(TpmManagerServiceTest, ReadUninitializedNvram) {
  auto callback = [this](const ReadSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SPACE_DOES_NOT_EXIST, reply.result());
    Quit();
  };
  ReadSpaceRequest request;
  service_->ReadSpace(request, base::Bind(callback));
  Run();
}

TEST_F(TpmManagerServiceTest, ReadWriteSpaceSuccess) {
  uint32_t nvram_index = 5;
  std::string nvram_data("nvram_data");
  auto define_callback = [](const DefineSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto write_callback = [](const WriteSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  auto read_callback = [nvram_data](const ReadSpaceReply& reply) {
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_EQ(nvram_data, reply.data());
  };
  DefineSpaceRequest define_request;
  define_request.set_index(nvram_index);
  define_request.set_size(nvram_data.size());
  service_->DefineSpace(define_request, base::Bind(define_callback));
  WriteSpaceRequest write_request;
  write_request.set_index(nvram_index);
  write_request.set_data(nvram_data);
  service_->WriteSpace(write_request, base::Bind(write_callback));
  ReadSpaceRequest read_request;
  read_request.set_index(nvram_index);
  service_->ReadSpace(read_request, base::Bind(read_callback));
  RunServiceWorkerAndQuit();
}

}  // namespace tpm_manager
