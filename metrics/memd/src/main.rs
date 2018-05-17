// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This program collects fine-grained memory stats around events of interest
// (such as browser tab discards) and saves them in a queue of "clip files",
// to be uploaded with other logs.
//
// The program has two modes: slow and fast poll.  In slow-poll mode, the
// program occasionally checks (every 2 seconds right now) whether it should go
// into fast-poll mode, because interesting events become possible.  When in
// fast-poll mode, the program collects memory stats frequently (every 0.1
// seconds right now) and stores them in a circular buffer.  When "interesting"
// events occur, the stats around each event are saved into a "clip" file.
// These files are also maintained as a queue, so only the latest N clips are
// available (N = 20 right now).

extern crate chrono;
extern crate libc;
extern crate dbus;

#[macro_use] extern crate log;
extern crate env_logger;

use chrono::prelude::*;
use {libc::__errno_location, libc::c_void};

use dbus::{Connection, BusType, WatchEvent};

use std::{io,mem,str,thread};
use std::cmp::max;
use std::error::Error;
use std::fmt;
use std::fs::{create_dir, File, OpenOptions};
use std::io::prelude::*;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::{Path, PathBuf};
// Not to be confused with chrono::Duration or the deprecated time::Duration.
use std::time::Duration;

const PAGE_SIZE: usize =            4096;   // old friend

const LOG_DIRECTORY: &str =         "/var/log/memd/";
const STATIC_PARAMETERS_LOG: &str = "memd.parameters";
const TESTING_ROOT: &str =          "./testing-root";
const LOW_MEM_SYSFS: &str =         "/sys/kernel/mm/chromeos-low_mem/";
const TRACING_SYSFS: &str =         "/sys/kernel/debug/tracing/";
const LOW_MEM_DEVICE: &str =        "/dev/chromeos-low-mem";
const MAX_CLIP_COUNT: usize =       20;

const COLLECTION_DELAY_MS: i64 = 5_000;       // Wait after event of interest.
const CLIP_COLLECTION_SPAN_MS: i64 = 10_000;  // ms worth of samples in a clip.
const SAMPLES_PER_SECOND: i64 = 10;           // Rate of fast sample collection.
const SAMPLING_PERIOD_MS: i64 = 1000 / SAMPLES_PER_SECOND;
const LOW_MEM_SAFETY_FACTOR: u32 = 3;         // Low-mem margin multiplier for polling.
const SLOW_POLL_PERIOD_DURATION: Duration = Duration::from_secs(2); // Sleep in slow-poll mode.
const FAST_POLL_PERIOD_DURATION: Duration =
    Duration::from_millis(SAMPLING_PERIOD_MS as u64); // Sleep duration in fast-poll mode.

// Print a warning if the fast-poll select lasts a lot longer than expected
// (which might happen because of huge load average and swap activity).
const UNREASONABLY_LONG_SLEEP: i64 = 10 * SAMPLING_PERIOD_MS;

// Size of sample queue.  The queue contains mostly timer events, in the amount
// determined by the clip span and the sampling rate.  It also contains other
// events, such as OOM kills etc., whose amount is expected to be smaller than
// the former.  Generously double the number of timer events to leave room for
// non-timer events.
const SAMPLE_QUEUE_LENGTH: usize =
    (CLIP_COLLECTION_SPAN_MS / 1000 * SAMPLES_PER_SECOND * 2) as usize;

// The names of fields of interest in /proc/vmstat.  They must be listed in
// the order in which they appear in /proc/vmstat.  When parsing the file,
// if a mandatory field is missing, the program panics.  A missing optional
// field (for instance, pgmajfault_f for some kernels) results in a value
// of 0.
const VMSTAT_VALUES_COUNT: usize = 7;           // Number of vmstat values we're tracking.
#[cfg(target_arch = "x86_64")]
const VMSTATS: [(&str, bool); VMSTAT_VALUES_COUNT] = [
    // name                     mandatory
    ("nr_pages_scanned",        true),
    ("pswpin",                  true),
    ("pswpout",                 true),
    ("pgalloc_dma32",           true),
    ("pgalloc_normal",          true),
    ("pgmajfault",              true),
    ("pgmajfault_f",            false),
];
#[cfg(any(target_arch = "arm", target_arch = "aarch64"))]
// The only difference from x86_64 is pgalloc_dma vs. pgalloc_dma32.
// Wish there was some more concise mechanism.
const VMSTATS: [(&str, bool); VMSTAT_VALUES_COUNT] = [
    // name                     mandatory
    ("nr_pages_scanned",        true),
    ("pswpin",                  true),
    ("pswpout",                 true),
    ("pgalloc_dma",             true),
    ("pgalloc_normal",          true),
    ("pgmajfault",              true),
    ("pgmajfault_f",            false),
];

type Result<T> = std::result::Result<T, Box<Error>>;

fn errno() -> i32 {
    // _errno_location() is trivially safe to call and dereferencing the
    // returned pointer is always safe because it's guaranteed to be valid and
    // point to thread-local data.  (Thanks to Zach for this comment.)
    unsafe { *__errno_location() }
}

// Returns the string for posix error number |err|.
fn strerror(err: i32) -> String {
    // safe to leave |buffer| uninitialized because we initialize it from strerror_r.
    let mut buffer: [u8; PAGE_SIZE] = unsafe { mem::uninitialized() };
    // |err| is valid because it is the libc errno at all call sites.  |buffer|
    // is converted to a valid address, and |buffer.len()| is a valid length.
    let n = unsafe {
        libc::strerror_r(err, &mut buffer[0] as *mut u8 as *mut libc::c_char, buffer.len())
    } as usize;
    match str::from_utf8(&buffer[..n]) {
        Ok(s) => s.to_string(),
        Err(e) => {
            // Ouch---an error while trying to process another error.
            // Very unlikely so we just panic.
            panic!("error {:?} converting {:?}", e, &buffer[..n]);
        }
    }
}

// Opens a file with optional mode flags (unfortunately named OpenOptions).  If
// the file does not exist, returns None.
fn open_with_flags(path: &Path, options_or_none: Option<&OpenOptions>) -> Result<Option<File>> {
    if !path.exists() {
        Ok(None)
    } else {
        let file = if let Some(ref options) = options_or_none {
            options.open(path)?
        } else {
            File::open(path)?
        };
        Ok(Some(file))
    }
}

// Converts the result of an integer expression |e| to modulo |n|. |e| may be
// negative. This differs from plain "%" in that the result of this function
// is always be between 0 and n-1.
fn modulo(e: isize, n: usize) -> usize {
    let nn = n as isize;
    let x = e % nn;
    (if x >= 0 { x } else { x + nn }) as usize
}

// Returns current uptime (active time since boot, in milliseconds)
fn now() -> i64 {
    let mut ts: libc::timespec;
    // clock_gettime is safe when passed a valid address and a valid enum.
    let result = unsafe {
        ts = mem::uninitialized();
        libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts as *mut libc::timespec)
    };
    assert_eq!(result, 0, "clock_gettime() failed!");
    ts.tv_sec as i64 * 1000 + ts.tv_nsec as i64 / 1_000_000
}

// Reads a string from the file named by |path|, representing a u32, and
// returns the value the strings represents.
fn read_int(path: &Path) -> Result<u32> {
    let mut file = File::open(path)?;
    let mut content = String::new();
    file.read_to_string(&mut content)?;
    Ok(content.trim().parse::<u32>()?)
}

struct FileWatcher {
    read_fds: libc::fd_set,
    inout_read_fds: libc::fd_set,
    max_fd: i32,
}

// Interface to the select(2) system call, for ready-to-read only.
impl FileWatcher {
    fn new() -> FileWatcher {
        // The fd sets don't need to be initialized to known values because
        // they are cleared by the following FD_ZERO calls, which are safe
        // because we're passing libc::fd_sets to them.
        let mut read_fds = unsafe { mem::uninitialized::<libc::fd_set>() };
        let mut inout_read_fds = unsafe { mem::uninitialized::<libc::fd_set>() };
        unsafe { libc::FD_ZERO(&mut read_fds); };
        unsafe { libc::FD_ZERO(&mut inout_read_fds); };
        FileWatcher { read_fds, inout_read_fds, max_fd: -1 }
    }

    fn set(&mut self, f: &File) -> Result<()> {
        self.set_fd(f.as_raw_fd())
    }

    // The ..._fd functions exist because other APIs (e.g. dbus) return RawFds
    // instead of Files.
    fn set_fd(&mut self, fd: RawFd) -> Result<()> {
        if fd >= libc::FD_SETSIZE as i32 {
            return Err("set_fd: fd too large".into());
        }
        // see comment for |set()|
        unsafe { libc::FD_SET(fd, &mut self.read_fds); }
        self.max_fd = max(self.max_fd, fd);
        Ok(())
    }

    fn clear_fd(&mut self, fd: RawFd) -> Result<()> {
        if fd >= libc::FD_SETSIZE as i32 {
            return Err("clear_fd: fd is too large".into());
        }
        // see comment for |set()|
        unsafe { libc::FD_CLR(fd, &mut self.read_fds); }
        Ok(())
    }

    fn has_fired(&mut self, f: &File) -> Result<bool> {
        self.has_fired_fd(f.as_raw_fd())
    }

    fn has_fired_fd(&mut self, fd: RawFd) -> Result<bool> {
        if fd >= libc::FD_SETSIZE as i32 {
            return Err("has_fired_fd: fd is too large".into());
        }
        // see comment for |set()|
        Ok(unsafe { libc::FD_ISSET(fd, &mut self.inout_read_fds) })
    }

    fn watch(&mut self, timeout: &Duration) -> Result<usize> {
        self.inout_read_fds = self.read_fds;
        let mut libc_timeout = libc::timeval {
            tv_sec: timeout.as_secs() as i64,
            tv_usec: timeout.subsec_nanos() as i64 / 1000,
        };
        let null = std::ptr::null_mut();
        // We're passing valid values and addresses.
        let n = unsafe {
            libc::select(self.max_fd + 1,
                         &mut self.inout_read_fds,
                         null,
                         null,
                         &mut libc_timeout as *mut libc::timeval)
        };
        match n {
            // into() puts the io::Error in a Box.
            -1 => Err(io::Error::last_os_error().into()),
            n => {
                if n < 0 {
                    Err(format!("unexpected return value {} from select", n).into())
                } else {
                    Ok(n as usize)
                }
            }
        }
    }
}

#[derive(Clone, Copy, Default)]
struct Sample {
    uptime: i64,                // system uptime in ms
    sample_type: SampleType,
    info: Sysinfo,
    runnables: u32,             // number of runnable processes
    available: u32,             // available RAM from low-mem notifier
    vmstat_values: [u32; VMSTAT_VALUES_COUNT],
}

impl Sample {
    // Outputs a sample.
    fn output(&self, out: &mut File) -> Result<()> {
        writeln!(out, "{}.{:02} {:6} {} {} {} {} {} {} {} {} {} {} {} {} {}",
               self.uptime / 1000,
               self.uptime % 1000 / 10,
               self.sample_type,
               self.info.0.loads[0],
               self.info.0.freeram,
               self.info.0.freeswap,
               self.info.0.procs,
               self.runnables,
               self.available,
               self.vmstat_values[0],
               self.vmstat_values[1],
               self.vmstat_values[2],
               self.vmstat_values[3],
               self.vmstat_values[4],
               self.vmstat_values[5],
               self.vmstat_values[6]
        )?;
        Ok(())
    }
}


#[derive(Copy, Clone)]
struct Sysinfo(libc::sysinfo);

impl Sysinfo {
    // Wrapper for sysinfo syscall.
    fn sysinfo() -> Result<Sysinfo> {
        let mut info: Sysinfo = Default::default();
        // sysinfo() is always safe when passed a valid pointer
        match unsafe { libc::sysinfo(&mut info.0 as *mut libc::sysinfo) } {
            0 => Ok(info),
            _ => Err(format!("sysinfo: {}", strerror(errno())).into())
        }
    }

    // Fakes sysinfo system call, for testing.
    fn fake_sysinfo() -> Result<Sysinfo> {
        let mut info: Sysinfo = Default::default();
        // Any numbers will do.
        info.0.loads[0] = 5;
        info.0.freeram = 42_000_000;
        info.0.freeswap = 84_000_000;
        info.0.procs = 1234;
        Ok(info)
    }
}

impl Default for Sysinfo {
    fn default() -> Sysinfo{
        // safe because sysinfo contains only numbers
        unsafe { mem::zeroed() }
    }
}

struct SampleQueue {
    samples: [Sample; SAMPLE_QUEUE_LENGTH],
    head: usize,   // points to latest entry
    count: usize,  // count of valid entries (min=0, max=SAMPLE_QUEUE_LENGTH)
}

impl SampleQueue {
    fn new() -> SampleQueue {
        let s: Sample = Default::default();
        SampleQueue { samples: [s; SAMPLE_QUEUE_LENGTH],
                      head: 0,
                      count: 0,
        }
    }

    // Returns self.head as isize, to make index calculations behave correctly
    // on underflow.
    fn ihead(&self) -> isize {
        self.head as isize
    }

    fn reset(&mut self) {
        self.head = 0;
        self.count = 0;
    }

    // Returns the next slot in the queue.  Always succeeds, since on overflow
    // it discards the LRU slot.
    fn next_slot(&mut self) -> &mut Sample {
        let slot = self.head;
        self.head = modulo(self.ihead() + 1, SAMPLE_QUEUE_LENGTH) as usize;
        if self.count < SAMPLE_QUEUE_LENGTH {
            self.count += 1;
        }
        &mut self.samples[slot]
    }

    fn sample(&self, i: isize) -> &Sample {
        assert!(i >= 0);
        // Subtract 1 because head points to the next free slot.
        assert!(modulo(self.ihead() - 1 - i, SAMPLE_QUEUE_LENGTH) <= self.count,
                "bad queue index: i {}, head {}, count {}", i, self.head, self.count);
        &self.samples[i as usize]
    }

    // Outputs to file |f| samples from |start_time| to the head.  Uses a start
    // time rather than a start index because when we start a clip we have to
    // do a time-based search anyway.
    fn output_from_time(&self, mut f: &mut File, start_time: i64) -> Result<()> {
        // For now just do a linear search. ;)
        let mut start_index = modulo(self.ihead() - 1, SAMPLE_QUEUE_LENGTH);
        debug!("output_from_time: start_time {}, head {}", start_time, start_index);
        while self.samples[start_index].uptime > start_time {
            debug!("output_from_time: seeking uptime {}, index {}",
                   self.samples[start_index].uptime, start_index);
            start_index = modulo(start_index as isize - 1, SAMPLE_QUEUE_LENGTH);
            if start_index == self.head {
                warn!("too many events in requested interval");
                break;
            }
        }

        let mut index = modulo(start_index as isize + 1, SAMPLE_QUEUE_LENGTH) as isize;
        while index != self.ihead() {
            debug!("output_from_time: outputting index {}", index);
            self.sample(index).output(&mut f)?;
            index = modulo(index + 1, SAMPLE_QUEUE_LENGTH) as isize;
        }
        Ok(())
    }
}

// Returns number of tasks in the run queue as reported in /proc/loadavg, which
// must be accessible via runnables_file.
fn get_runnables(runnables_file: &File) -> Result<u32> {
    // It is safe to leave the content of buffer uninitialized because we read
    // into it and only look at the part of it initialized by reading.
    let mut buffer: [u8; PAGE_SIZE] = unsafe { mem::uninitialized() };
    let content = pread(runnables_file, &mut buffer[..])?;
    // Example: "0.81 0.66 0.86 22/3873 7043" (22 runnables here).  The format is
    // fixed, so just skip the first 15 characters.
    if content.len() < 16 {
        return Err("unexpected /proc/loadavg format".into());
    }
    let (value, _) = parse_int_prefix(&content[15..])?;
    Ok(value)
}

// Converts the initial digit characters of |s| to the value of the number they
// represent.  Returns the number of characters scanned.
fn parse_int_prefix(s: &str) -> Result<(u32, usize)> {
    let mut result = 0;
    let mut count = 0;
    for c in s.chars() {
        let x = c.to_digit(10);
        match x {
            Some (d) => {
                result = result * 10 + d;
                count += 1;
            },
            None => { break; }
        }
    }
    if count == 0 {
        Err("cannot parse int".into())
    } else {
        Ok((result, count))
    }
}

// Writes string |string| to file |path|, in append mode if |append| is true.
// Panics on errors.
fn write_string(string: &str, path: &Path, append: bool) -> Result<()> {
    let mut f = OpenOptions::new().write(true).append(append).open(path)?;
    f.write_all(string.as_bytes())?;
    Ok(())
}

// Reads selected values from |file| (which should be opened to /proc/vmstat)
// as specified in |VMSTATS|, and stores them in |vmstat_values|.  The format of
// the vmstat file is (<name> <integer-value>\n)+.
fn get_vmstats(file: &File, vmstat_values: &mut [u32]) -> Result<()> {
    // Safe because we read into the buffer, then borrow the part of it that
    // was filled.
    let mut buffer: [u8; PAGE_SIZE] = unsafe { mem::uninitialized() };
    let content = pread(file, &mut buffer[..])?;
    let mut rest = &content[..];
    for (i, &(name, mandatory)) in VMSTATS.iter().enumerate() {
        let found_name = rest.find(name);
        match found_name {
            Some(name_pos) => {
                rest = &rest[name_pos..];
                let found_space = rest.find(' ');
                match found_space {
                    Some(space_pos) => {
                        // Skip name and space.
                        rest = &rest[space_pos+1..];
                        let (value, pos) = parse_int_prefix(rest)?;
                        vmstat_values[i] = value;
                        rest = &rest[pos..];
                    }
                    None => {
                        return Err("unexpected vmstat file syntax".into());
                    }
                }
            }
            None => {
                if mandatory {
                    return Err(format!("missing '{}' from vmstats", name).into());
                } else {
                    vmstat_values[i] = 0;
                }
            }
        }
    }
    Ok(())
}

fn pread_u32(f: &File) -> Result<u32> {
    let mut buffer: [u8; 20] = [0; 20];
    // pread is safe to call with valid addresses and buffer length.
    let length = unsafe {
        libc::pread(f.as_raw_fd(),
                    &mut buffer[0] as *mut u8 as *mut c_void,
                    buffer.len(),
                    0)
    };
    if length < 0 {
        return Err("bad pread_u32".into());
    }
    if length == 0 {
        return Err("empty pread_u32".into());
    }
    Ok(String::from_utf8_lossy(&buffer[..length as usize]).trim().parse::<u32>()?)
}

// Reads the content of file |f| starting at offset 0, up to |PAGE_SIZE|,
// stores it in |read_buffer|, and returns a slice of it as a string.
fn pread<'a>(f: &File, buffer: &'a mut [u8]) -> Result<&'a str> {
    // pread is safe to call with valid addresses and buffer length.
    let length = unsafe {
        libc::pread(f.as_raw_fd(), &mut buffer[0] as *mut u8 as *mut c_void, buffer.len(), 0)
    };
    if length == 0 {
        return Err("unexpected null pread".into());
    }
    if length < 0 {
        return Err(format!("pread failed: {}", strerror(errno())).into());
    }
    // Sysfs files contain only single-byte characters, so from_utf8_unchecked
    // would be safe for those files.  However, there's no guarantee that this
    // is only called on sysfs files.
    // str::from_utf8(&buffer[..l as usize]).or(Err("bad utf8 data".into()))
    Ok(str::from_utf8(&buffer[..length as usize])?)
}

struct Watermarks { min: u32, low: u32, high: u32 }

struct ZoneinfoFile(File);

impl ZoneinfoFile {
    // Computes and returns the watermark values from /proc/zoneinfo.
    fn read_watermarks(&mut self) -> Result<Watermarks> {
        let mut min = 0;
        let mut low = 0;
        let mut high = 0;
        let mut content = String::new();
        self.0.read_to_string(&mut content)?;
        for line in content.lines() {
            let items = line.split_whitespace().collect::<Vec<_>>();
            match items[0] {
                "min" => min += items[1].parse::<u32>()?,
                "low" => low += items[1].parse::<u32>()?,
                "high" => high += items[1].parse::<u32>()?,
                _ => {}
            }
        }
        Ok(Watermarks { min, low, high })
    }
}

// The main object for the program state.
struct SamplerState {
    testing: bool,                // Set to true when running integration test
    always_poll_fast: bool,       // When true, program stays fast poll mode
    paths: Paths,                 // Paths of system files used by the program
    dbus_connection: dbus::Connection,        // To receive Chrome signals
    // File descriptors for poll().
    dbus_fds: Vec<RawFd>,
    // Low-memory notification margin, assumed to remain constant in a boot
    // session.
    low_mem_margin: u32,
    sample_header: String,         // The text at the beginning of each clip file
    files: Files,                  // Files kept open by the program
    watcher: FileWatcher,          // Maintains a set of files being watched for select()
    low_mem_watcher: FileWatcher,  // A watcher for the low-mem device
    clip_counter: usize,           // Index of next clip file (also part of file name)
    sample_queue: SampleQueue,     // A running queue of samples of vm quantities
    current_available: u32,        // Amount of "available" memory (in MB) at last reading
    current_time: i64,             // Wall clock in ms at last reading
    collecting: bool,              // True if we're in the middle of collecting a clip
}

impl SamplerState {

    // Refresh cached time.  This should be called after system calls, which
    // can potentially block, but not if it is unused before the next call.
    fn refresh_time(&mut self) {
        self.current_time = now();
    }

    // Collect a sample using the latest time snapshot.
    fn enqueue_sample(&mut self, sample_type: SampleType) -> Result<()> {
        let time = self.current_time;  // to pacify the borrow checker
        self.enqueue_sample_at_time(sample_type, time)
    }

    // Collects a sample of memory manager stats and adds it to the sample
    // queue, possibly overwriting an old value.  |sample_type| indicates the
    // type of sample, and |time| the system uptime at the time the sample was
    // collected.
    fn enqueue_sample_at_time(&mut self, sample_type: SampleType, time: i64) -> Result<()> {
        {
            let sample: &mut Sample = self.sample_queue.next_slot();
            sample.uptime = time;
            sample.sample_type = sample_type;
            sample.available = self.current_available;
            sample.runnables = get_runnables(&self.files.runnables_file)?;
            sample.info = if self.testing { Sysinfo::fake_sysinfo()? } else { Sysinfo::sysinfo()? };
            get_vmstats(&self.files.vmstat_file, &mut sample.vmstat_values)?;
        }
        self.refresh_time();
        Ok(())
    }

    // This is called to check if any of the dbus file descriptors fired.  It
    // checks the incoming messages and produces a sample for each relevant
    // message.
    //
    // If signals of type "ChromeEvent" have been delivered to DBUS_INTERFACE,
    // this function enqueues one event for each such signal.  The signal's
    // payload is a string, which is used to derive the type of the event.
    //
    // For debugging:
    //
    // dbus-monitor --system type=signal,interface=org.chromium.EventLogger
    //
    // dbus-send --system --type=signal / org.chromium.EventLogger.ChromeEvent string:tab-discard
    //
    fn process_chrome_signals(&mut self) -> Result<bool> {
        // Check all delivered messages, and count the messages of interest of
        // each kind.  Then create events for those messages.  Counting first
        // simplifies the borrowing.
        let mut tab_discard_count = 0;
        let mut oom_kill_browser_count = 0;
        {
            for &fd in self.dbus_fds.iter() {
                if !self.watcher.has_fired_fd(fd)? {
                    continue;
                }
                let handle = self.dbus_connection.watch_handle(
                    fd, WatchEvent::Readable as libc::c_uint);
                for connection_item in handle {
                    // Only consider signals.
                    if let dbus::ConnectionItem::Signal(ref message) = connection_item {
                        // Only consider signals with "ChromeEvent" members.
                        if let Some(member) = message.member() {
                            if &*member != "ChromeEvent" {
                                continue;
                            }
                        }
                        let items = &message.get_items();
                        if items.is_empty() {
                            continue;
                        }
                        let item = &message.get_items()[0];
                        match item {
                            &dbus::MessageItem::Str(ref s) => match &s[..] {
                                "exit-gracefully" => {
                                    if self.testing {
                                        std::process::exit(0);
                                    } else {
                                        warn!("ignoring exit-gracefully request");
                                    }
                                },
                                "tab-discard" | "oom-kill" => {
                                    match sample_type_from_signal(s) {
                                        SampleType::TabDiscard => tab_discard_count += 1,
                                        SampleType::OomKillBrowser => oom_kill_browser_count += 1,
                                        x => panic!("unexpected sample type {:?}", x),
                                    }
                                },
                                _ => { warn!("unexpected chrome event type: {}", s); }
                            }
                            _ => warn!("ignoring chrome event with non-string arg1"),
                        }
                    }
                }
            }
        }
        for _ in 0..tab_discard_count {
            self.enqueue_sample(SampleType::TabDiscard)?;
        }
        for _ in 0..oom_kill_browser_count {
            self.enqueue_sample(SampleType::TabDiscard)?;
        }
        Ok(tab_discard_count + oom_kill_browser_count > 0)
    }

    // Reads oom-kill events from trace_pipe (passed as |fd|) and adds them to
    // the sample queue.  Returns true if clip collection must be started.
    //
    // TODO(semenzato will open bug): trace pipe can only be used by a single
    // process, so this needs to be demonized.
    fn process_oom_traces(&mut self) -> Result<bool> {
        // It is safe to leave the content of |buffer| uninitialized because we
        // read into it.
        let mut buffer: [u8; PAGE_SIZE] = unsafe { mem::uninitialized() };
        // Safe because we're reading into a valid buffer up to its size.
        let length = unsafe {
            libc::read(self.files.trace_pipe_file.as_raw_fd(),
                       &mut buffer[0] as *mut u8 as *mut c_void, buffer.len())
        };
        if length < 0 {
            return Err("error reading trace pipe".into());
        }
        if length == 0 {
            return Err("unexpected empty trace pipe".into());
        }
        let ul = length as usize;
        // Note: the following assertion is necessary because if the trace output
        // with the oom kill event straddles two buffers, the parser can fail in
        // mysterious ways.
        if ul >= PAGE_SIZE {
            return Err("unexpectedly large input from trace_pipe".into());
        }
        // We trust the kernel to only produce valid utf8 strings.
        let mut rest = unsafe { str::from_utf8_unchecked(&buffer[..ul]) };
        let mut oom_happened = false;

        // Parse output of trace_pipe.  There may be multiple oom kills, and who
        // knows what else.
        loop {
            /* The oom line from trace_pipe looks like this (including the 8
             * initial spaces):
            chrome-13700 [001] .... 867348.061651: oom_kill_process <-out_of_memory
             */
            let oom_kill_function_name = "oom_kill_process";
            if let Some(function_index) = rest.find(oom_kill_function_name) {
                oom_happened = true;
                let slice = &rest[..function_index - 2];
                rest = &rest[function_index + oom_kill_function_name.len() ..];
                // Grab event time.
                let reported_time: f64 = match slice.rfind(' ') {
                    Some(time_index) => {
                        let time_string = &slice[time_index + 1 ..];
                        match time_string.parse() {
                            Ok(value) => value,
                            Err(_) => return Err("cannot parse float from trace pipe".into())
                        }
                    },
                    None => return Err("cannot find space in trace pipe".into())
                };
                // Add two samples, one with the OOM time as reported by the kernel, the
                // other with the current time (firing of the trace event).  Collecting the
                // data twice is a waste, but this is a rare event.
                let time =  (reported_time * 1000.0) as i64;
                self.enqueue_sample_at_time(SampleType::OomKillKernel, time)?;
                self.enqueue_sample(SampleType::OomKillTrace)?;
            } else {
                break;
            }
        }
        Ok(oom_happened)
    }

    // Creates or overwrites a file in the memd log directory containing
    // quantities of interest.
    fn log_static_parameters(&self) -> Result<()> {
        let mut out = &mut OpenOptions::new()
            .write(true)
            .create(true)
            .open(&self.paths.static_parameters)
            .unwrap();
        fprint_datetime(out)?;
        let low_mem_margin = read_int(&self.paths.low_mem_margin).unwrap_or(0);
        writeln!(out, "margin {}", low_mem_margin)?;

        let psv = &self.paths.procsysvm;
        log_from_procfs(&mut out, psv, "min_filelist_kbytes")?;
        log_from_procfs(&mut out, psv, "min_free_kbytes")?;
        log_from_procfs(&mut out, psv, "extra_free_kbytes")?;

        let mut zoneinfo = ZoneinfoFile { 0: File::open(&self.paths.zoneinfo)? };
        let watermarks = zoneinfo.read_watermarks()?;
        writeln!(out, "min_water_mark_kbytes {}", watermarks.min * 4)?;
        writeln!(out, "low_water_mark_kbytes {}", watermarks.low * 4)?;
        writeln!(out, "high_water_mark_kbytes {}", watermarks.high * 4)?;
        Ok(())
    }

    // Returns true if the program should go back to slow-polling mode (or stay
    // in that mode).  Returns false otherwise.  Relies on |self.collecting|
    // and |self.current_available| being up-to-date.
    fn should_poll_slowly(&self) -> bool {
        !self.collecting &&
            !self.always_poll_fast &&
            self.current_available > LOW_MEM_SAFETY_FACTOR * self.low_mem_margin
    }

    // Sits mostly idle and checks available RAM at low frequency.  Returns
    // when available memory gets "close enough" to the tab discard margin.
    fn slow_poll(&mut self) -> Result<()> {
        debug!("entering slow poll at {}", self.current_time);
        // Idiom for do ... while.
        while {
            thread::sleep(SLOW_POLL_PERIOD_DURATION);
            if let Some(ref available_file) = self.files.available_file_option.as_ref() {
                self.current_available = pread_u32(available_file)?;
            }
            self.refresh_time();
            self.should_poll_slowly()
        } {}
        Ok(())
    }

    // Collects timer samples at fast rate.  Also collects event samples.
    // Samples contain various system stats related to kernel memory
    // management.  The samples are placed in a circular buffer.  When
    // something "interesting" happens, (for instance a tab discard, or a
    // kernel OOM-kill) the samples around that event are saved into a "clip
    // file".
    fn fast_poll(&mut self) -> Result<()> {
        let mut earliest_start_time = self.current_time;
        debug!("entering fast poll at {}", earliest_start_time);

        // Collect the first timer sample immediately upon entering.
        self.enqueue_sample(SampleType::Timer)?;
        // Keep track if we're in a low-mem state.  Initially assume we are
        // not.
        let mut in_low_mem = false;
        // |clip_{start,end}_time| are the collection start and end time for
        // the current clip.  Their value is valid only when |self.collecting|
        // is true.
        let mut clip_start_time = self.current_time;
        let mut clip_end_time = self.current_time;
        // |final_collection_time| indicates when we should stop collecting
        // samples for any clip (either the current one, or the next one).  Its
        // value is valid only when |self.collecting| is true.
        let mut final_collection_time = self.current_time;
        let null_duration = Duration::from_secs(0);

        // |self.collecting| is true when we're in the middle of collecting a clip
        // because something interesting has happened.
        self.collecting = false;

        // Poll loop.
        loop {
            // Assume event is not interesting (since most events
            // aren't). Change this to true for some event types.
            let mut event_is_interesting = false;

            let watch_start_time = self.current_time;
            let fired_count = self.watcher.watch(&FAST_POLL_PERIOD_DURATION)?;
            if let Some(ref available_file) = self.files.available_file_option.as_ref() {
                self.current_available = pread_u32(available_file)?;
            }
            self.refresh_time();

            // Record a sample when we sleep too long.  Such samples are
            // somewhat redundant as they could be deduced from the log, but we
            // wish to make it easier to detect such (rare, we hope)
            // occurrences.
            if watch_start_time > self.current_time + UNREASONABLY_LONG_SLEEP {
                warn!("woke up at {} after unreasonable {} sleep",
                      self.current_time, self.current_time - watch_start_time);
                self.enqueue_sample(SampleType::Sleeper)?;
            }

            if fired_count == 0 {
                // Timer event.
                self.enqueue_sample(SampleType::Timer)?;
            } else {
                // The low_mem file is level-triggered, not edge-triggered (my bad)
                // so we don't watch it when memory stays low, because it would
                // fire continuously.  Instead we poll it at every event, and when
                // we detect a low-to-high transition we start watching it again.
                // Unfortunately this requires an extra select() call: however we
                // don't expect to spend too much time in this state (and when we
                // do, this is the least of our worries).
                let low_mem_has_fired = match self.files.low_mem_file_option {
                    Some(ref low_mem_file) => self.watcher.has_fired(low_mem_file)?,
                    _ => false,
                };
                if low_mem_has_fired {
                    debug!("entering low mem at {}", self.current_time);
                    in_low_mem = true;
                    self.enqueue_sample(SampleType::EnterLowMem)?;
                    let fd = self.files.low_mem_file_option.as_ref().unwrap().as_raw_fd();
                    self.watcher.clear_fd(fd)?;
                } else if in_low_mem && self.low_mem_watcher.watch(&null_duration)? == 0 {
                    // Refresh time since we may have blocked.  (That should
                    // not happen often because currently we're below the
                    // minimum timeslice.)
                    self.refresh_time();
                    debug!("leaving low mem at {}", self.current_time);
                    in_low_mem = false;
                    self.enqueue_sample(SampleType::LeaveLowMem)?;
                    self.watcher.set(&self.files.low_mem_file_option.as_ref().unwrap())?;
                }

                // Check for Chrome events.
                if self.process_chrome_signals()? {
                    debug!("chrome signals at {}", self.current_time);
                    event_is_interesting = true;
                }

                // Check for OOM-kill event.
                if self.watcher.has_fired(&self.files.trace_pipe_file)? {
                    if self.process_oom_traces()? {
                        debug!("oom trace at {}", self.current_time);
                        event_is_interesting = true;
                    }
                }
            }

            // Arrange future saving of samples around interesting events.
            if event_is_interesting {
                // Update the time intervals to ensure we include all samples
                // of interest in a clip.  If we're not in the middle of
                // collecting a clip, start one.  If we're in the middle of
                // collecting a clip which can be extended, do that.
                final_collection_time = self.current_time + COLLECTION_DELAY_MS;
                if self.collecting {
                    // Check if the current clip can be extended.
                    if clip_end_time < clip_start_time + CLIP_COLLECTION_SPAN_MS {
                        clip_end_time = final_collection_time.min(
                            clip_start_time + CLIP_COLLECTION_SPAN_MS);
                        debug!("extending clip to {}", clip_end_time);
                    }
                } else {
                    // Start the clip collection.
                    self.collecting = true;
                    clip_start_time = earliest_start_time
                        .max(self.current_time - COLLECTION_DELAY_MS);
                    clip_end_time = self.current_time + COLLECTION_DELAY_MS;
                    debug!("starting new clip from {} to {}", clip_start_time, clip_end_time);
                }
            }

            // Check if it is time to save the samples into a file.
            if self.collecting && self.current_time > clip_end_time - SAMPLING_PERIOD_MS {
                // Save the clip to disk.
                debug!("[{}] saving clip: ({} ... {}), final {}",
                       self.current_time, clip_start_time, clip_end_time, final_collection_time);
                self.save_clip(clip_start_time)?;
                self.collecting = false;
                earliest_start_time = clip_end_time;
                // Need to schedule another collection?
                if final_collection_time > clip_end_time {
                    // Continue collecting by starting a new clip.  Note that
                    // the clip length may be less than CLIP_COLLECTION_SPAN.
                    // This happens when event spans overlap, and also if we
                    // started fast sample collection just recently.
                    assert!(final_collection_time <= clip_end_time + CLIP_COLLECTION_SPAN_MS);
                    clip_start_time = clip_end_time;
                    clip_end_time = final_collection_time;
                    self.collecting = true;
                    debug!("continue collecting with new clip ({} {})",
                           clip_start_time, clip_end_time);
                    // If we got stuck in the select() for a very long time
                    // because of system slowdown, it may be time to collect
                    // this second clip as well.  But we don't bother, since
                    // this is unlikely, and we can collect next time around.
                    if self.current_time > clip_end_time {
                        debug!("heavy slowdown: postponing collection of ({}, {})",
                               clip_start_time, clip_end_time);
                    }
                }
            }
            if self.should_poll_slowly() {
                break;
            }
        }
        Ok(())
    }

    // Returns the clip file pathname to be used after the current one,
    // and advances the clip counter.
    fn next_clip_path(&mut self) -> PathBuf {
        let name = format!("memd.clip{:03}.log", self.clip_counter);
        self.clip_counter = modulo(self.clip_counter as isize + 1, MAX_CLIP_COUNT);
        self.paths.log_directory.join(name)
    }

    // Finds and sets the starting value for the clip counter in this session.
    // The goal is to preserve the most recent clips (if any) from previous
    // sessions.
    fn find_starting_clip_counter(&mut self) -> Result<()> {
        self.clip_counter = 0;
        let mut previous_time = std::time::UNIX_EPOCH;
        loop {
            let path = self.next_clip_path();
            if !path.exists() {
                break;
            }
            let md = std::fs::metadata(path)?;
            let time = md.modified()?;
            if time < previous_time {
                break;
            } else {
                previous_time = time;
            }
        }
        // We found the starting point, but the counter has already advanced so we
        // need to backtrack one step.
        self.clip_counter = modulo(self.clip_counter as isize - 1, MAX_CLIP_COUNT);
        Ok(())
    }

    // Stores samples of interest in a new clip log, and remove those samples
    // from the queue.
    fn save_clip(&mut self, start_time: i64) -> Result<()> {
        let path = self.next_clip_path();
        let mut out = &mut OpenOptions::new().write(true).create(true).open(path)?;

        // Print courtesy header.  The first line is the current time.  The
        // second line lists the names of the variables in the following lines,
        // in the same order.
        fprint_datetime(out)?;
        out.write_all(self.sample_header.as_bytes())?;
        // Output samples from |start_time| to the head.
        self.sample_queue.output_from_time(&mut out, start_time)?;
        // The queue is now empty.
        self.sample_queue.reset();
        Ok(())
    }
}

// Sets up tracing system to report OOM-kills.  Returns file to monitor for
// OOM-kill events.
//
// TODO(crbug.com/840574): change this to get events from tracing daemon
// instead (probably via D-Bus).
fn oom_trace_setup(paths: &Paths, testing: bool) -> Result<File> {
    write_string("function", &paths.current_tracer, false)?;
    write_string("oom_kill_process", &paths.set_ftrace_filter, true)?;
    let tracing_enabled_file = File::open(&paths.tracing_enabled)?;
    if pread_u32(&tracing_enabled_file)? != 1 {
        return Err("tracing is disabled".into());
    }
    let tracing_on_file = File::open(&paths.tracing_on)?;
    if pread_u32(&tracing_on_file)? != 1 {
        return Err("tracing is off".into());
    }
    // When testing, the fifo must be opened RW.  Otherwise it will be always
    // ready to read after the first writer leaves, because it's ready to read an
    // EOF :/.
    let mut options = OpenOptions::new();
    options.read(true);
    options.custom_flags(libc::O_NONBLOCK);
    if testing {
        options.write(true);
    }
    Ok(options.open(&paths.trace_pipe)?)
}

// Prints |name| and value of entry /pros/sys/vm/|name| (or 0, if the entry is
// missing) to file |out|.
fn log_from_procfs(out: &mut File, dir: &Path, name: &str) -> Result<()> {
    let procfs_path = dir.join(name);
    let value = read_int(&procfs_path).unwrap_or(0);
    writeln!(out, "{} {}", name, value)?;
    Ok(())
}

// Outputs readable date and time to file |out|.
fn fprint_datetime(out: &mut File) -> Result<()> {
    let local: DateTime<Local> = Local::now();
    writeln!(out, "{}", local)?;
    Ok(())
}

#[derive(Copy, Clone, Debug)]
enum SampleType {
    EnterLowMem,
    LeaveLowMem,
    OomKillBrowser,
    OomKillKernel,
    OomKillTrace,
    Sleeper,
    TabDiscard,
    Timer,
    Uninitialized,
    Unknown,
}

impl Default for SampleType {
    fn default() -> SampleType {
        SampleType::Uninitialized
    }
}

impl fmt::Display for SampleType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.name())
    }
}

impl SampleType {
    // Returns the 6-character(max) identifier for a sample type.
    fn name(&self) -> &'static str {
        match *self {
            SampleType::EnterLowMem => "lowmem",
            SampleType::LeaveLowMem => "lealow",
            SampleType::OomKillBrowser => "oomkll",
            SampleType::OomKillKernel => "keroom",
            SampleType::OomKillTrace => "traoom",
            SampleType::Sleeper => "sleepr",
            SampleType::TabDiscard => "discrd",
            SampleType::Timer => "timer",
            SampleType::Uninitialized => "UNINIT",
            SampleType::Unknown => "UNKNWN",
        }
    }
}

// Returns the sample type for a Chrome signal (the string payload in the DBus
// message).
fn sample_type_from_signal(signal: &str) -> SampleType {
    match signal {
        "oom-kill" => SampleType::OomKillBrowser,
        "tab-discard" => SampleType::TabDiscard,
        _ => SampleType::Unknown,
    }
}

// Sets up a D-Bus connection to receive information from Chrome about events
// of interest (e.g. tab discards).
fn dbus_setup() -> Result<dbus::Connection> {
    let c = Connection::get_private(BusType::System)?;
    let _m = c.add_match(concat!("type='signal',",
                                 "interface='org.chromium.EventLogger',",
                                 "member='ChromeEvent'"));
    Ok(c)
}

// Path names of various system files, mostly in /proc, /sys, and /dev.  They
// are collected into this struct because they get special values when testing.
struct Paths {
    vmstat: PathBuf,
    available: PathBuf,
    runnables: PathBuf,
    low_mem_margin: PathBuf,
    low_mem_device: PathBuf,
    current_tracer: PathBuf,
    set_ftrace_filter: PathBuf,
    tracing_enabled: PathBuf,
    tracing_on: PathBuf,
    trace_pipe: PathBuf,
    log_directory: PathBuf,
    static_parameters: PathBuf,
    zoneinfo: PathBuf,
    procsysvm: PathBuf,
}

// Returns a file name that replaces |name| when testing.
fn test_filename(testing: bool, name: &str) -> String {
    if testing {
        TESTING_ROOT.to_string() + name
    } else {
        name.to_owned()
    }
}

// This macro constructs a "normal" Paths object when |testing| is false, and
// one that mimics a root filesystem in a local directory when |testing| is
// true.
macro_rules! make_paths {
    ($testing:expr,
     $($name:ident : $e:expr,)*
    ) =>
        ( Paths { $($name: PathBuf::from(test_filename($testing, &($e).to_string()))),* } )
}

// All files that are to be left open go here.  We keep them open to reduce the
// number of syscalls.  They are mostly files in /proc and /sys.
struct Files {
    vmstat_file: File,
    runnables_file: File,
    trace_pipe_file: File,
    // These files might not exist.
    available_file_option: Option<File>,
    low_mem_file_option: Option<File>,
}

fn build_sample_header() -> String {
    let mut s = "uptime type load freeram freeswap procs runnables available".to_string();
    for vmstat in VMSTATS.iter() {
        s = s + " " + vmstat.0;
    }
    s + "\n"
}

fn main() {
    env_logger::init();

    let mut testing = false;
    let mut always_poll_fast = false;

    debug!("memd started");

    let args: Vec<String> = std::env::args().collect();
    for arg in &args[1..] {
        match arg.as_ref() {
            "test" => testing = true,
            "always-poll-fast" => always_poll_fast = true,
            _ => panic!("usage: memd [test|always-poll-fast]*")
        }
    }

    let paths = make_paths!(
        testing,
        vmstat:            "/proc/vmstat",
        available:         LOW_MEM_SYSFS.to_string() + "available",
        runnables:         "/proc/loadavg",
        low_mem_margin:    LOW_MEM_SYSFS.to_string() + "margin",
        low_mem_device:    LOW_MEM_DEVICE,
        current_tracer:    TRACING_SYSFS.to_string() + "current_tracer",
        set_ftrace_filter: TRACING_SYSFS.to_string() + "set_ftrace_filter",
        tracing_enabled:   TRACING_SYSFS.to_string() + "tracing_enabled",
        tracing_on:        TRACING_SYSFS.to_string() + "tracing_on",
        trace_pipe:        TRACING_SYSFS.to_string() + "trace_pipe",
        log_directory:     LOG_DIRECTORY,
        static_parameters: LOG_DIRECTORY.to_string() + STATIC_PARAMETERS_LOG,
        zoneinfo:          "/proc/zoneinfo",
        procsysvm:         "/proc/sys/vm/",
    );

    // Make sure /var/log/memd exists.  Create it if not.  Assume /var/log
    // exists.  Panic on errors.
    if !paths.log_directory.exists() {
        create_dir(&paths.log_directory)
            .expect(&format!("cannot create log directory {:?}", paths.log_directory));
    }

    let dbus_connection = dbus_setup().expect("cannot setup dbus");
    let dbus_fds: Vec<i32> = dbus_connection.watch_fds().iter().map(|w| w.fd()).collect();
    let mut low_mem_file_flags = OpenOptions::new();
    low_mem_file_flags.custom_flags(libc::O_NONBLOCK);
    low_mem_file_flags.read(true);
    let low_mem_file_option = open_with_flags(&paths.low_mem_device, Some(&low_mem_file_flags))
        .expect("error opening low-mem file");
    let mut watcher = FileWatcher::new();
    let mut low_mem_watcher = FileWatcher::new();

    if let Some(ref low_mem_file) = low_mem_file_option.as_ref() {
        watcher.set(&low_mem_file).expect("cannot watch low-mem fd");
        low_mem_watcher.set(&low_mem_file).expect("cannot set low-mem watcher");
    }
    for fd in dbus_fds.iter().by_ref() {
        watcher.set_fd(*fd).expect("cannot watch dbus fd");
    }

    let trace_pipe_file = oom_trace_setup(&paths, testing).expect("trace setup error");
    watcher.set(&trace_pipe_file).expect("cannot watch trace_pipe");

    let files = Files {
        vmstat_file: File::open(&paths.vmstat).unwrap(),
        runnables_file: File::open(&paths.runnables).unwrap(),
        available_file_option: open_with_flags(&paths.available, None)
            .expect("error opening available file"),
        trace_pipe_file,
        low_mem_file_option,
    };

    let sample_header = build_sample_header();

    let mut sampler = SamplerState {
        testing,
        always_poll_fast,
        dbus_connection,
        dbus_fds,
        low_mem_margin: read_int(&paths.low_mem_margin).unwrap_or(0),
        paths,
        sample_header,
        files,
        watcher,
        low_mem_watcher,
        sample_queue: SampleQueue::new(),
        clip_counter: 0,
        collecting: false,
        current_available: 0,
        current_time: 0,
    };

    sampler.find_starting_clip_counter().expect("cannot find starting clip counter");
    sampler.log_static_parameters().expect("cannot log static parameters");

    loop {
        // Alternate between slow and fast poll.
        sampler.slow_poll().expect("slow poll error");
        sampler.fast_poll().expect("fast poll error");
    }
}