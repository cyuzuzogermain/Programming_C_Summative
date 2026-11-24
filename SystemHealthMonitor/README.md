# Linux System Health and Resource Monitoring Manager

`system_monitor.sh` is an interactive Bash application. It monitors CPU, memory, disk, processes and uptime, generates timestamped health reports, and keeps an activity log of everything the user does.

```bash
chmod +x system_monitor.sh
./system_monitor.sh
```

Requirements: Bash 4 or newer and standard Linux utilities (`ps`, `df`, `awk`, `sort`, `grep`, `date`, `uname`). The script reads `/proc/stat`, `/proc/meminfo`, `/proc/loadavg` and `/proc/uptime` directly. If any of these tools or files are missing, the script falls back to an alternative or reports the problem (see [Error handling](#error-handling)).

## Menu

| Option | Operation |
|--------|-----------|
| 1 | Display system information: hostname, OS, kernel, architecture, uptime, date and time, current user |
| 2 | Monitor CPU utilisation. Asks for a threshold, samples CPU usage for 1 second, shows the top 10 processes, and flags any process above the threshold. |
| 3 | Monitor memory utilisation. Shows total, used and available memory plus a utilisation %, warns when the threshold is exceeded, and lists the top 5 memory users. |
| 4 | Monitor disk utilisation. Shows every real filesystem with an OK or WARNING status against the threshold. |
| 5 | Process viewer, with three sub-options: list the top N or all processes, search by name (partial and case-insensitive), or search by PID |
| 6 | Terminate a process: validates the PID, checks protection and permission, asks for confirmation, sends SIGTERM, and offers SIGKILL if the process is still running |
| 7 | Generate a health report in a directory the user chooses (default `./reports`). The file is named `health_report_YYYYmmdd_HHMMSS.txt`. |
| 8 | View the activity log, with an optional keyword filter such as `WARNING` |
| 9 | Clear the activity log after confirmation |
| 0 | Exit |

Thresholds default to CPU 80%, memory 80% and disk 85%. At each monitoring prompt you can type a new value, or press Enter to keep the current one.

## Script structure

The script is split into six numbered sections:

1. **Configuration and globals**: paths, default thresholds, the protected-process list, and colours (used only when output is a terminal).
2. **Helpers**:
   - Output: `info`, `warn`, `error`, `print_header`.
   - Command checks: `has_cmd`, `require_cmd`.
   - Logging: `log_activity`.
   - Input: `prompt`, which reads a line and exits cleanly at end of input; `confirm`; `pause`.
   - Validation: `is_positive_int`, `validate_threshold`, `ask_threshold`.
   - Number handling: `float_gt` and `human_kb`.
3. **Data collection** (no user interaction):
   - `get_hostname`, `get_os_name`, `get_uptime`.
   - `collect_cpu_stats`: takes two `/proc/stat` samples one second apart.
   - `collect_memory_stats`: reads `/proc/meminfo`.
   - `get_disk_usage`: runs `df -hP`, excluding tmpfs, devtmpfs, squashfs, overlay and efivarfs.
4. **Rendering**: `print_process_table`, `print_system_info_section`, `print_cpu_section`, `print_memory_section`, `print_disk_table` and `disks_over_threshold`. The interactive monitors and the report builder share these functions, so no formatting code is repeated.
5. **Menu operations**: one function per menu option, for example `monitor_cpu` and `terminate_process`. `generate_report` and `build_report` handle reports; `view_activity_log` and `clear_activity_log` handle the log.
6. **Main**: `main` sets a Ctrl+C trap, starts logging, checks dependencies, and runs the menu loop.

### How CPU usage is calculated

`/proc/stat` holds cumulative CPU time counters. The script reads them twice, one second apart:

```
usage % = (1 - Δidle / Δtotal) × 100
```

Here `idle` is idle plus iowait, and `total` is the sum of user, nice, system, idle, iowait, irq, softirq and steal. If `/proc/stat` cannot be read, the script falls back to the idle value reported by `top -bn1`.

The per-process `%CPU` comes from `ps`. It is the process's average since it started, not an instantaneous value.

### How memory usage is calculated

```
used = MemTotal - MemAvailable
utilisation % = used / MemTotal × 100
```

`MemAvailable` counts reclaimable cache as available, which matches what `free` shows. On kernels without `MemAvailable`, the script uses `MemFree` instead.

## Commands used

| Command | Purpose |
|---------|---------|
| `ps -eo pid,user,pcpu,pmem,comm` and `ps -p PID -o ...` | Process lists, search, and details for a single PID |
| `sort -k3,3nr` and `head` | Rank processes by CPU or memory use |
| `awk` | Parse `/proc` files and `df` output, and do decimal arithmetic and comparisons |
| `df -hP -x <type>` | Disk usage for real filesystems |
| `uname -r -m -o`, `hostname`, `/etc/os-release`, `uptime -p` | System information |
| `nproc` and `/proc/loadavg` | CPU core count and load average |
| `kill -TERM`, `kill -KILL`, `kill -0` | Terminate a process and check whether it is still running |
| `grep -iF` | Literal, case-insensitive filtering of the log |
| `date`, `mkdir -p`, `touch`, `id -un` | Timestamps, directory creation and the current user |

## Activity log

The log is stored at `logs/activity.log`. You can move it by setting the `SYSMON_LOG_DIR` environment variable. Each line has this format:

```
[2026-11-23 16:32:51] [WARNING] [user:germain] Memory usage 74.8% exceeded threshold 60%
```

The levels are `INFO`, `WARNING`, `ERROR` and `REPORT`. The log records:

- session start and end
- every monitoring result
- threshold changes
- warnings
- process searches
- termination attempts, refusals and results
- report generation, including failures
- invalid menu selections

## Error handling

| Situation | Handling |
|-----------|----------|
| Invalid menu option | An error message is shown, the log records it, and the menu is shown again |
| Invalid threshold (not a number, 0, or above 100) | The script rejects it and asks again. Pressing Enter keeps the current value. |
| Missing utilities | Missing tools are listed at start-up. `require_cmd` stops the affected feature with a clear message. Fallbacks: `hostname` → `/etc/hostname` → `uname -n`; `/proc/meminfo` → `free`; `/proc/stat` → `top`; `uptime -p` → `/proc/uptime` |
| Invalid or missing PID | The script rejects input that is not a number and reports a PID that doesn't exist |
| Protected processes | Refused: PID 1 and 2, kernel threads (children of PID 2), the script and its parent shell, and a list of critical daemons such as systemd, sshd, dbus and the display server |
| Insufficient permission | A non-root user cannot terminate another user's process (the script compares the process UID with `$EUID`). A failed `kill` is also reported. |
| Report directory problems | If the path is not a directory, cannot be created, or is not writable, the script reports it and logs it. It asks before creating a new directory. |
| Log file not writable | The script falls back to `/tmp/system_monitor_<user>.log` and warns once |
| Confirmation | Terminating a process, sending SIGKILL and clearing the log all require an explicit `y` |
| End of input or Ctrl+C | The script exits cleanly and logs that the session ended or was interrupted |

## Evidence (`samples/`)

| File | Contents |
|------|----------|
| `demo_run.txt` | Full terminal session covering every menu option. It includes invalid input, refusal to terminate PID 1 and a root-owned process, a real process being terminated, and report-directory errors. |
| `sample_health_report.txt` | A generated health report |
| `sample_activity.log` | The activity log from that session |

Screenshots for the submission: run `./system_monitor.sh` in a terminal and capture options 2, 3, 4, 5 and 6. The colour output is only visible in a real terminal.
