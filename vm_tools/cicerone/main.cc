// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <brillo/syslog_logging.h>

#include "vm_tools/cicerone/service.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::MessageLoopForIO message_loop;

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (argc != 1) {
    LOG(ERROR) << "Unexpected command line arguments";
    return EXIT_FAILURE;
  }

  base::RunLoop run_loop;

  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;

  auto service = vm_tools::cicerone::Service::Create(
      run_loop.QuitClosure(), new dbus::Bus(std::move(opts)));
  CHECK(service);

  run_loop.Run();

  return 0;
}
