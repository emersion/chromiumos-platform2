//
// Copyright (C) 2012 The Android Open Source Project
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

#include "shill/shims/net_diags_upload.h"

#include <stdlib.h>

#include <string>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <chromeos/syslog_logging.h>

using std::string;

namespace switches {

static const char kHelp[] = "help";
static const char kUpload[] = "upload";

// The help message shown if help flag is passed to the program.
static const char kHelpMessage[] = "\n"
    "Available Switches:\n"
    "  --help\n"
    "    Show this help message.\n"
    "  --upload\n"
    "    Upload the diagnostics logs.\n";

}  // namespace switches

namespace shill {

namespace shims {

void StashLogs() {
  // Captures the last 10000 lines in the log regardless of log rotation. I.e.,
  // prints the log files in timestamp sorted order and gets the tail of the
  // output.
  string cmdline =
      base::StringPrintf("/bin/cat $(/bin/ls -rt /var/log/net.*log) | "
                         "/bin/tail -10000 > %s", kStashedNetLog);
  if (system(cmdline.c_str())) {
    LOG(ERROR) << "Unable to stash net.log.";
  } else {
    LOG(INFO) << "net.log stashed.";
  }
}

}  // namespace shims

}  // namespace shill

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return 0;
  }

  chromeos::InitLog(chromeos::kLogToSyslog | chromeos::kLogHeader);

  shill::shims::StashLogs();

  if (cl->HasSwitch(switches::kUpload)) {
    // Crash so that crash_reporter can upload the logs.
    abort();
  }

  return 0;
}