// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shill_daemon.h"
#include "shill/logging.h"

#include <sysexits.h>

#include <base/bind.h>


namespace shill {

ShillDaemon::ShillDaemon(const base::Closure& startup_callback,
                         const shill::DaemonTask::Settings& settings,
                         Config* config)
    : daemon_task_(settings, config), startup_callback_(startup_callback) {}

ShillDaemon::~ShillDaemon() {}

int ShillDaemon::OnInit() {
  // Manager DBus interface will get registered as part of this init call.
  int return_code = brillo::Daemon::OnInit();
  if (return_code != EX_OK) {
    return return_code;
  }

  daemon_task_.Init();

  // Signal that we've acquired all resources.
  startup_callback_.Run();

  return EX_OK;
}

void ShillDaemon::OnShutdown(int* return_code) {
  LOG(INFO) << "ShillDaemon received shutdown.";

  if (!daemon_task_.Quit(base::Bind(&DaemonTask::BreakTerminationLoop,
                                    base::Unretained(&daemon_task_)))) {
    // Run a message loop to allow shill to complete its termination
    // procedures. This is different from the secondary loop in
    // brillo::Daemon. This loop will run until we explicitly
    // breakout of the loop, whereas the secondary loop in
    // brillo::Daemon will run until no more tasks are posted on the
    // loop.  This allows asynchronous D-Bus method calls to complete
    // before exiting.
    brillo::MessageLoop::current()->Run();
  }

  brillo::Daemon::OnShutdown(return_code);
}

}  // namespace shill
