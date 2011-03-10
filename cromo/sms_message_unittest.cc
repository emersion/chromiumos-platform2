// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Unit tests for SMS message creation

#include "sms_message.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

const uint8_t pdu[] = {
  0x07, 0x91, 0x21, 0x04, 0x44, 0x29, 0x61, 0xf4,
  0x04, 0x0b, 0x91, 0x61, 0x71, 0x95, 0x72, 0x91,
  0xf8, 0x00, 0x00, 0x11, 0x20, 0x82, 0x11, 0x05,
  0x05, 0x0a,
  // user data:
  0x6a, 0xc8, 0xb2, 0xbc, 0x7c, 0x9a, 0x83, 0xc2,
  0x20, 0xf6, 0xdb, 0x7d, 0x2e, 0xcb, 0x41, 0xed,
  0xf2, 0x7c, 0x1e, 0x3e, 0x97, 0x41, 0x1b, 0xde,
  0x06, 0x75, 0x4f, 0xd3, 0xd1, 0xa0, 0xf9, 0xbb,
  0x5d, 0x06, 0x95, 0xf1, 0xf4, 0xb2, 0x9b, 0x5c,
  0x26, 0x83, 0xc6, 0xe8, 0xb0, 0x3c, 0x3c, 0xa6,
  0x97, 0xe5, 0xf3, 0x4d, 0x6a, 0xe3, 0x03, 0xd1,
  0xd1, 0xf2, 0xf7, 0xdd, 0x0d, 0x4a, 0xbb, 0x59,
  0xa0, 0x79, 0x7d, 0x8c, 0x06, 0x85, 0xe7, 0xa0,
  0x00, 0x28, 0xec, 0x26, 0x83, 0x2a, 0x96, 0x0b,
  0x28, 0xec, 0x26, 0x83, 0xbe, 0x60, 0x50, 0x78,
  0x0e, 0xba, 0x97, 0xd9, 0x6c, 0x17
};

TEST(SmsMessage, CreateFromPdu) {
  SmsMessage* sms = SmsMessage::CreateMessage(pdu, sizeof(pdu));

  ASSERT_TRUE(NULL != sms);
  EXPECT_EQ("12404492164", sms->smsc_address());
  EXPECT_EQ("16175927198", sms->sender_address());
  EXPECT_EQ("110228115050-08", sms->timestamp());
  EXPECT_EQ("Here's a longer message [{with some extended characters}] "
            "thrown in, such as £ and ΩΠΨ and §¿ as well.", sms->text());
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}
