#!/usr/bin/env bash
# ============================================================================
#  Linux System Health and Resource Monitoring Manager
# ============================================================================
#  An interactive, menu-driven shell application that monitors CPU, memory,
#  disk, processes and uptime, generates timestamped health reports and keeps
#  an activity log of everything the user does.
#
#  Usage:   ./system_monitor.sh
#  Env:     SYSMON_LOG_DIR   override the directory that holds activity.log
#
#  Structure
#    1. Configuration and globals
#    2. Output / input helpers      (colours, prompts, validation, logging)
#    3. Data collection functions   (collect_* / get_*  -> no user interaction)
#    4. Rendering functions         (print_* -> reused by monitors AND reports)
#    5. Menu operations             (one function per menu option)
#    6. Main menu loop
# ============================================================================

set -o pipefail          # a failing command inside a pipeline fails the pipeline
                         # (no `set -e`: errors are handled explicitly)

# ----------------------------------------------------------------------------
# 1. Configuration and globals
# ----------------------------------------------------------------------------
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOG_DIR="${SYSMON_LOG_DIR:-$SCRIPT_DIR/logs}"
LOG_FILE="$LOG_DIR/activity.log"
DEFAULT_REPORT_DIR="$SCRIPT_DIR/reports"

# Default warning thresholds (percent). The user can change them at runtime.
CPU_THRESHOLD=80
MEM_THRESHOLD=80
DISK_THRESHOLD=85

# Process names that may never be terminated from this tool.
PROTECTED_PROCESSES=(
    systemd init kthreadd systemd-journald systemd-logind systemd-udevd udevd
    dbus-daemon dbus-broker sshd login agetty NetworkManager polkitd
    Xorg Xwayland gnome-shell kwin_wayland gdm sddm
)

# Values filled in by the collect_* functions.
CPU_USAGE="" CPU_CORES="" LOAD_AVG=""
MEM_TOTAL_KB=0 MEM_AVAIL_KB=0 MEM_USED_KB=0 MEM_PERCENT=0
SWAP_TOTAL_KB=0 SWAP_USED_KB=0

LOG_WRITE_FAILED=0

# Colours only when writing to a terminal (keeps reports/logs clean).
if [[ -t 1 ]]; then
    RED=$'\e[31m' GREEN=$'\e[32m' YELLOW=$'\e[33m' CYAN=$'\e[36m'
    BOLD=$'\e[1m' RESET=$'\e[0m'
else
    RED="" GREEN="" YELLOW="" CYAN="" BOLD="" RESET=""
fi

# ----------------------------------------------------------------------------
# 2. Output / input helpers
# ----------------------------------------------------------------------------

info()    { printf '%s[INFO]%s %s\n'    "$GREEN"  "$RESET" "$*"; }
warn()    { printf '%s[WARNING]%s %s\n' "$YELLOW" "$RESET" "$*"; }
error()   { printf '%s[ERROR]%s %s\n'   "$RED"    "$RESET" "$*" >&2; }
print_header() { printf '\n%s%s===== %s =====%s\n' "$BOLD" "$CYAN" "$1" "$RESET"; }

# has_cmd NAME -> success if the command is installed
has_cmd() { command -v "$1" >/dev/null 2>&1; }

# require_cmd NAME... -> prints an error and fails if any command is missing
require_cmd() {
    local cmd missing=()
    for cmd in "$@"; do
        has_cmd "$cmd" || missing+=("$cmd")
    done
    if (( ${#missing[@]} > 0 )); then
        error "Required command(s) not available: ${missing[*]}"
        log_activity ERROR "Missing utilities: ${missing[*]}"
        return 1
    fi
}

# log_activity LEVEL MESSAGE... -> appends a timestamped line to the log
log_activity() {
    local level=$1; shift
    if ! printf '[%s] [%-7s] [user:%s] %s\n' \
            "$(date '+%Y-%m-%d %H:%M:%S')" "$level" "${USER:-$(id -un)}" "$*" \
            >> "$LOG_FILE" 2>/dev/null; then
        if (( LOG_WRITE_FAILED == 0 )); then
            error "Cannot write to log file '$LOG_FILE'. Activity will not be recorded."
            LOG_WRITE_FAILED=1
        fi
    fi
}

# prompt VAR "message" -> reads one line into VAR (exits cleanly on EOF)
prompt() {
    local __var=$1 __msg=$2 __reply
    printf '%s' "$__msg"
    if ! IFS= read -r __reply; then
        printf '\n'
        info "End of input reached."
        exit_application
    fi
    printf -v "$__var" '%s' "$__reply"
}

# confirm "question" -> success only on an explicit yes
confirm() {
    local answer
    prompt answer "$1 [y/N]: "
    [[ $answer =~ ^([Yy]|[Yy][Ee][Ss])$ ]]
}

# pause -> waits for Enter, but only when a human is at the keyboard
pause() {
    if [[ -t 0 ]]; then
        local _
        read -r -p "Press Enter to return to the menu..." _
    fi
}

# is_positive_int VALUE -> success for 1, 2, 3, ...
is_positive_int() { [[ $1 =~ ^[0-9]+$ ]] && (( 10#$1 > 0 )); }

# validate_threshold VALUE -> success for a whole number 1..100
validate_threshold() { [[ $1 =~ ^[0-9]{1,3}$ ]] && (( 10#$1 >= 1 && 10#$1 <= 100 )); }

# ask_threshold "Label" VAR_NAME -> lets the user change a threshold
ask_threshold() {
    local label=$1 var=$2 input
    while true; do
        prompt input "Enter $label warning threshold in % (1-100) [Enter keeps ${!var}%]: "
        [[ -z $input ]] && return 0
        if validate_threshold "$input"; then
            printf -v "$var" '%d' "$((10#$input))"
            info "$label threshold set to ${!var}%."
            log_activity INFO "$label threshold set to ${!var}%"
            return 0
        fi
        error "Invalid threshold '$input'. Enter a whole number between 1 and 100."
    done
}

# float_gt A B -> success if A > B (bash cannot compare decimals itself)
float_gt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > b) }'; }

# human_kb KIB -> "15.4 GiB"
human_kb() {
    awk -v k="$1" 'BEGIN {
        split("KiB MiB GiB TiB", unit, " "); i = 1
        while (k >= 1024 && i < 4) { k /= 1024; i++ }
        printf "%.1f %s", k, unit[i] }'
}

# ----------------------------------------------------------------------------
# 3. Data collection (no user interaction)
# ----------------------------------------------------------------------------

get_hostname() {
    if has_cmd hostname; then hostname
    elif [[ -r /etc/hostname ]]; then cat /etc/hostname
    else uname -n
    fi
}

get_os_name() {
    if [[ -r /etc/os-release ]]; then
        ( . /etc/os-release && echo "${PRETTY_NAME:-$NAME}" )
    elif has_cmd lsb_release; then
        lsb_release -ds
    else
        uname -o
    fi
}

get_uptime() {
    if uptime -p >/dev/null 2>&1; then
        uptime -p
    elif [[ -r /proc/uptime ]]; then
        awk '{ s = int($1); printf "up %d days, %d hours, %d minutes\n",
               s / 86400, (s % 86400) / 3600, (s % 3600) / 60 }' /proc/uptime
    else
        echo "unavailable"
    fi
}

# Reads idle and total jiffies from the aggregate "cpu" line of /proc/stat.
read_cpu_jiffies() {
    awk '/^cpu / { idle = $5 + $6; total = 0
                   for (i = 2; i <= 9; i++) total += $i
                   print idle, total; exit }' /proc/stat
}

# collect_cpu_stats -> sets CPU_USAGE (%), CPU_CORES and LOAD_AVG.
# Usage is measured over a 1 second sample of /proc/stat; `top` is a fallback.
collect_cpu_stats() {
    local idle1 total1 idle2 total2
    CPU_USAGE=""
    if [[ -r /proc/stat ]]; then
        read -r idle1 total1 < <(read_cpu_jiffies)
        sleep 1
        read -r idle2 total2 < <(read_cpu_jiffies)
        CPU_USAGE=$(awk -v i1="$idle1" -v t1="$total1" -v i2="$idle2" -v t2="$total2" \
            'BEGIN { dt = t2 - t1; if (dt <= 0) dt = 1
                     printf "%.1f", (1 - (i2 - i1) / dt) * 100 }')
    elif has_cmd top; then
        CPU_USAGE=$(top -bn1 | awk -F',' '/Cpu\(s\)/ {
            for (i = 1; i <= NF; i++) if ($i ~ /id/) { gsub(/[^0-9.]/, "", $i); printf "%.1f", 100 - $i } }')
    fi

    if has_cmd nproc; then CPU_CORES=$(nproc)
    else CPU_CORES=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo "?")
    fi

    if [[ -r /proc/loadavg ]]; then
        LOAD_AVG=$(awk '{ print $1 ", " $2 ", " $3 }' /proc/loadavg)
    else
        LOAD_AVG="unavailable"
    fi

    [[ -n $CPU_USAGE ]]
}

# collect_memory_stats -> sets the MEM_* and SWAP_* globals.
collect_memory_stats() {
    local swap_free=0
    if [[ -r /proc/meminfo ]]; then
        read -r MEM_TOTAL_KB MEM_AVAIL_KB SWAP_TOTAL_KB swap_free < <(awk '
            /^MemTotal:/     { t  = $2 }
            /^MemAvailable:/ { a  = $2 }
            /^MemFree:/      { f  = $2 }
            /^SwapTotal:/    { st = $2 }
            /^SwapFree:/     { sf = $2 }
            END { if (a == "") a = f; print t, a, st + 0, sf + 0 }' /proc/meminfo)
    elif has_cmd free; then
        read -r MEM_TOTAL_KB MEM_AVAIL_KB < <(free -k | awk '/^Mem:/ { print $2, $7 }')
        read -r SWAP_TOTAL_KB swap_free   < <(free -k | awk '/^Swap:/ { print $2, $4 }')
    else
        error "Neither /proc/meminfo nor 'free' is available."
        return 1
    fi

    if ! is_positive_int "$MEM_TOTAL_KB"; then
        error "Could not read total memory."
        return 1
    fi
    MEM_USED_KB=$(( MEM_TOTAL_KB - MEM_AVAIL_KB ))
    SWAP_USED_KB=$(( SWAP_TOTAL_KB - swap_free ))
    MEM_PERCENT=$(awk -v u="$MEM_USED_KB" -v t="$MEM_TOTAL_KB" 'BEGIN { printf "%.1f", u * 100 / t }')
}

# get_disk_usage -> prints "filesystem size used avail use% mount" lines
# for real (non-virtual) filesystems, without the df header.
get_disk_usage() {
    local out
    out=$(df -hP -x tmpfs -x devtmpfs -x squashfs -x overlay -x efivarfs 2>/dev/null) \
        || out=$(df -hP 2>/dev/null) \
        || return 1
    awk 'NR > 1' <<< "$out"
}

# ----------------------------------------------------------------------------
# 4. Rendering (shared by interactive monitors and health reports)
# ----------------------------------------------------------------------------

# print_process_table SORT_FIELD COUNT -> top processes sorted by pcpu or pmem
print_process_table() {
    local sort_field=$1 count=$2 column=3
    [[ $sort_field == pmem ]] && column=4
    printf '%-8s %-12s %6s %6s  %s\n' "PID" "USER" "%CPU" "%MEM" "COMMAND"
    # The awk filter hides the short-lived `ps` used to take this snapshot.
    ps -eo pid=,user=,pcpu=,pmem=,comm= 2>/dev/null \
        | awk '$5 != "ps"' \
        | sort -k"$column","$column"nr \
        | head -n "$count" \
        | awk '{ printf "%-8s %-12s %6s %6s  %s\n", $1, $2, $3, $4, $5 }'
}

print_system_info_section() {
    printf '%-18s %s\n' "Hostname:"       "$(get_hostname)"
    printf '%-18s %s\n' "Operating system:" "$(get_os_name)"
    printf '%-18s %s\n' "Kernel version:" "$(uname -r)"
    printf '%-18s %s\n' "Architecture:"   "$(uname -m)"
    printf '%-18s %s\n' "Uptime:"         "$(get_uptime)"
    printf '%-18s %s\n' "Date and time:"  "$(date '+%A, %d %B %Y %H:%M:%S %Z')"
    printf '%-18s %s\n' "Current user:"   "$(id -un)"
}

print_cpu_section() {
    printf '%-18s %s%%\n' "CPU usage:"    "$CPU_USAGE"
    printf '%-18s %s\n'   "CPU cores:"    "$CPU_CORES"
    printf '%-18s %s\n'   "Load average:" "$LOAD_AVG (1, 5, 15 min)"
}

print_memory_section() {
    printf '%-18s %s\n'   "Total memory:"     "$(human_kb "$MEM_TOTAL_KB")"
    printf '%-18s %s\n'   "Used memory:"      "$(human_kb "$MEM_USED_KB")"
    printf '%-18s %s\n'   "Available memory:" "$(human_kb "$MEM_AVAIL_KB")"
    printf '%-18s %s%%\n' "Utilisation:"      "$MEM_PERCENT"
    if (( SWAP_TOTAL_KB > 0 )); then
        printf '%-18s %s of %s\n' "Swap used:" "$(human_kb "$SWAP_USED_KB")" "$(human_kb "$SWAP_TOTAL_KB")"
    else
        printf '%-18s %s\n' "Swap:" "not configured"
    fi
}

# print_disk_table THRESHOLD DISK_LINES -> table with a status column
print_disk_table() {
    local threshold=$1 lines=$2
    printf '%-28s %7s %7s %7s %5s  %-20s %s\n' "FILESYSTEM" "SIZE" "USED" "AVAIL" "USE%" "MOUNTED ON" "STATUS"
    awk -v thr="$threshold" '{
        pct = $5; gsub("%", "", pct)
        status = (pct + 0 >= thr) ? "WARNING" : "OK"
        printf "%-28s %7s %7s %7s %5s  %-20s %s\n", $1, $2, $3, $4, $5, $6, status
    }' <<< "$lines"
}

# disks_over_threshold THRESHOLD DISK_LINES -> "mount (NN%)" per line
disks_over_threshold() {
    awk -v thr="$2" '{ pct = $5; gsub("%", "", pct)
                       if (pct + 0 >= thr) print $6 " (" $5 ")" }' <<< "$1"
}

# ----------------------------------------------------------------------------
# 5. Menu operations
# ----------------------------------------------------------------------------

display_system_info() {
    print_header "SYSTEM INFORMATION"
    print_system_info_section
    log_activity INFO "Displayed system information"
}

monitor_cpu() {
    print_header "CPU UTILISATION"
    ask_threshold "CPU" CPU_THRESHOLD
    info "Sampling CPU usage for 1 second..."
    if ! collect_cpu_stats; then
        error "Unable to measure CPU usage (/proc/stat and top unavailable)."
        log_activity ERROR "CPU monitoring failed: no data source"
        return
    fi

    print_cpu_section
    log_activity INFO "CPU usage ${CPU_USAGE}% (threshold ${CPU_THRESHOLD}%)"
    if float_gt "$CPU_USAGE" "$CPU_THRESHOLD"; then
        warn "CPU usage ${CPU_USAGE}% exceeds the ${CPU_THRESHOLD}% threshold!"
        log_activity WARNING "CPU usage ${CPU_USAGE}% exceeded threshold ${CPU_THRESHOLD}%"
    else
        info "CPU usage is within the ${CPU_THRESHOLD}% threshold."
    fi

    require_cmd ps sort || return
    printf '\n%sTop 10 processes by CPU usage:%s\n' "$BOLD" "$RESET"
    print_process_table pcpu 10

    # Individual processes above the threshold
    local heavy
    heavy=$(ps -eo pid=,pcpu=,comm= | awk -v thr="$CPU_THRESHOLD" '$3 != "ps" && $2 + 0 > thr { printf "PID %s (%s) %s%%\n", $1, $3, $2 }')
    if [[ -n $heavy ]]; then
        warn "Processes using more than ${CPU_THRESHOLD}% CPU:"
        printf '  %s\n' "$heavy"
        log_activity WARNING "High-CPU processes: $(tr '\n' ';' <<< "$heavy")"
    else
        info "No single process is above ${CPU_THRESHOLD}% CPU."
    fi
}

monitor_memory() {
    print_header "MEMORY UTILISATION"
    ask_threshold "Memory" MEM_THRESHOLD
    collect_memory_stats || { log_activity ERROR "Memory monitoring failed"; return; }

    print_memory_section
    log_activity INFO "Memory usage ${MEM_PERCENT}% (threshold ${MEM_THRESHOLD}%)"
    if float_gt "$MEM_PERCENT" "$MEM_THRESHOLD"; then
        warn "Memory utilisation ${MEM_PERCENT}% exceeds the ${MEM_THRESHOLD}% threshold!"
        log_activity WARNING "Memory usage ${MEM_PERCENT}% exceeded threshold ${MEM_THRESHOLD}%"
    else
        info "Memory utilisation is within the ${MEM_THRESHOLD}% threshold."
    fi

    require_cmd ps sort || return
    printf '\n%sTop 5 processes by memory usage:%s\n' "$BOLD" "$RESET"
    print_process_table pmem 5
}

monitor_disk() {
    print_header "DISK UTILISATION"
    require_cmd df || return
    ask_threshold "Disk" DISK_THRESHOLD

    local disks over
    if ! disks=$(get_disk_usage) || [[ -z $disks ]]; then
        error "Unable to read filesystem usage."
        log_activity ERROR "Disk monitoring failed"
        return
    fi

    print_disk_table "$DISK_THRESHOLD" "$disks"
    over=$(disks_over_threshold "$disks" "$DISK_THRESHOLD")
    echo
    if [[ -n $over ]]; then
        warn "Filesystems at or above ${DISK_THRESHOLD}% capacity:"
        printf '  %s\n' "$over"
        log_activity WARNING "Disks over ${DISK_THRESHOLD}%: $(tr '\n' ' ' <<< "$over")"
    else
        info "All filesystems are below ${DISK_THRESHOLD}% capacity."
        log_activity INFO "Disk check OK (threshold ${DISK_THRESHOLD}%)"
    fi
}

# print_pid_details PID -> detailed row for one process (fails if missing)
print_pid_details() {
    local row
    row=$(ps -p "$1" -o pid=,ppid=,user=,stat=,pcpu=,pmem=,etime=,comm= 2>/dev/null) || return 1
    [[ -n $row ]] || return 1
    printf '%-8s %-8s %-12s %-5s %6s %6s %12s  %s\n' "PID" "PPID" "USER" "STAT" "%CPU" "%MEM" "ELAPSED" "COMMAND"
    awk '{ printf "%-8s %-8s %-12s %-5s %6s %6s %12s  %s\n", $1, $2, $3, $4, $5, $6, $7, $8 }' <<< "$row"
}

list_processes() {
    local count total
    prompt count "How many processes to show (sorted by CPU) [default 20, 'all']: "
    count=${count:-20}
    total=$(ps -e --no-headers 2>/dev/null | wc -l)
    if [[ $count == all ]]; then
        count=$total
    elif ! is_positive_int "$count"; then
        error "Invalid number '$count'. Showing 20."
        count=20
    fi
    info "Total running processes: $total"
    print_process_table pcpu "$count"
    log_activity INFO "Listed $count of $total running processes"
}

search_process_by_name() {
    local name matches
    prompt name "Enter process name (or part of it): "
    if [[ -z $name ]]; then
        error "Process name cannot be empty."
        return
    fi
    # index() does a literal, case-insensitive substring match (no regex surprises)
    matches=$(ps -eo pid=,user=,pcpu=,pmem=,comm= | awk -v n="$name" \
        'index(tolower($5), tolower(n)) { printf "%-8s %-12s %6s %6s  %s\n", $1, $2, $3, $4, $5 }')
    if [[ -z $matches ]]; then
        warn "No running process matches '$name'."
        log_activity INFO "Process search '$name': no matches"
        return
    fi
    printf '%-8s %-12s %6s %6s  %s\n' "PID" "USER" "%CPU" "%MEM" "COMMAND"
    printf '%s\n' "$matches"
    info "$(wc -l <<< "$matches") matching process(es)."
    log_activity INFO "Process search '$name': $(wc -l <<< "$matches") match(es)"
}

search_process_by_pid() {
    local pid
    prompt pid "Enter PID: "
    if ! is_positive_int "$pid"; then
        error "Invalid PID '$pid'. A PID is a positive whole number."
        log_activity ERROR "Invalid PID entered for search: '$pid'"
        return
    fi
    if ! print_pid_details "$pid"; then
        warn "No process with PID $pid is running."
        log_activity INFO "PID search $pid: not found"
        return
    fi
    log_activity INFO "PID search $pid: found"
}

view_processes() {
    require_cmd ps sort || return
    local choice
    while true; do
        print_header "PROCESS VIEWER"
        echo " 1) List running processes"
        echo " 2) Search by process name"
        echo " 3) Search by PID"
        echo " 0) Back to main menu"
        prompt choice "Select an option: "
        case $choice in
            1) list_processes ;;
            2) search_process_by_name ;;
            3) search_process_by_pid ;;
            0) return ;;
            *) error "Invalid option '$choice'. Choose 0-3." ;;
        esac
    done
}

# is_protected_process PID NAME PPID -> success if the process must not be killed
is_protected_process() {
    local pid=$1 name=$2 ppid=$3 protected
    (( pid <= 2 )) && return 0                          # init/systemd and kthreadd
    (( ppid == 2 )) && return 0                         # kernel threads
    (( pid == $$ || pid == PPID )) && return 0          # this script and its shell
    for protected in "${PROTECTED_PROCESSES[@]}"; do
        [[ $name == "$protected" ]] && return 0
    done
    return 1
}

terminate_process() {
    print_header "TERMINATE A PROCESS"
    require_cmd ps kill || return

    local pid name ppid owner_uid
    prompt pid "Enter the PID of the process to terminate: "
    if ! is_positive_int "$pid"; then
        error "Invalid PID '$pid'. A PID is a positive whole number."
        log_activity ERROR "Terminate: invalid PID '$pid'"
        return
    fi
    pid=$((10#$pid))

    if ! print_pid_details "$pid"; then
        error "No process with PID $pid exists."
        log_activity ERROR "Terminate: PID $pid not found"
        return
    fi

    name=$(ps -p "$pid" -o comm= | awk '{ print $1 }')
    ppid=$(ps -p "$pid" -o ppid= | tr -d ' ')
    owner_uid=$(ps -p "$pid" -o uid= | tr -d ' ')

    if is_protected_process "$pid" "$name" "$ppid"; then
        error "PID $pid ($name) is a protected system process and will not be terminated."
        log_activity WARNING "Terminate refused: PID $pid ($name) is protected"
        return
    fi

    if (( EUID != 0 && owner_uid != EUID )); then
        error "Permission denied: PID $pid ($name) belongs to another user (UID $owner_uid)."
        log_activity WARNING "Terminate refused: no permission for PID $pid ($name)"
        return
    fi

    if ! confirm "Are you sure you want to terminate PID $pid ($name)?"; then
        info "Termination cancelled."
        log_activity INFO "Terminate cancelled by user for PID $pid ($name)"
        return
    fi

    if ! kill -TERM "$pid" 2>/dev/null; then
        error "Failed to send SIGTERM to PID $pid (it may have exited, or permission was denied)."
        log_activity ERROR "Terminate failed: SIGTERM to PID $pid ($name)"
        return
    fi

    # Give the process up to 3 seconds to exit gracefully.
    local waited
    for waited in 1 2 3; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done

    if kill -0 "$pid" 2>/dev/null; then
        warn "PID $pid is still running after SIGTERM."
        if confirm "Force kill with SIGKILL?"; then
            if kill -KILL "$pid" 2>/dev/null; then
                info "SIGKILL sent to PID $pid ($name)."
                log_activity WARNING "Process PID $pid ($name) force killed (SIGKILL)"
            else
                error "Failed to send SIGKILL to PID $pid."
                log_activity ERROR "SIGKILL failed for PID $pid ($name)"
            fi
        else
            log_activity INFO "PID $pid ($name) ignored SIGTERM; user declined SIGKILL"
        fi
    else
        info "Process $pid ($name) terminated successfully."
        log_activity INFO "Process PID $pid ($name) terminated (SIGTERM)"
    fi
}

# build_report -> writes the full health report to stdout
build_report() {
    local disks over
    echo "=================================================================="
    echo "                    SYSTEM HEALTH REPORT"
    echo "=================================================================="
    echo "Generated: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "Thresholds: CPU ${CPU_THRESHOLD}% | Memory ${MEM_THRESHOLD}% | Disk ${DISK_THRESHOLD}%"
    echo
    echo "---------------------- SYSTEM INFORMATION ------------------------"
    print_system_info_section
    echo
    echo "------------------------------ CPU -------------------------------"
    print_cpu_section
    echo
    echo "Top 5 processes by CPU:"
    print_process_table pcpu 5
    echo
    echo "----------------------------- MEMORY -----------------------------"
    print_memory_section
    echo
    echo "Top 5 processes by memory:"
    print_process_table pmem 5
    echo
    echo "------------------------------ DISK ------------------------------"
    disks=$(get_disk_usage)
    print_disk_table "$DISK_THRESHOLD" "$disks"
    echo
    echo "---------------------------- WARNINGS ----------------------------"
    local warnings=0
    if float_gt "$CPU_USAGE" "$CPU_THRESHOLD"; then
        echo "* CPU usage ${CPU_USAGE}% exceeds ${CPU_THRESHOLD}%"; warnings=$((warnings + 1))
    fi
    if float_gt "$MEM_PERCENT" "$MEM_THRESHOLD"; then
        echo "* Memory usage ${MEM_PERCENT}% exceeds ${MEM_THRESHOLD}%"; warnings=$((warnings + 1))
    fi
    over=$(disks_over_threshold "$disks" "$DISK_THRESHOLD")
    if [[ -n $over ]]; then
        while IFS= read -r line; do
            echo "* Disk $line at or above ${DISK_THRESHOLD}%"; warnings=$((warnings + 1))
        done <<< "$over"
    fi
    (( warnings == 0 )) && echo "No warnings. System is healthy."
    echo
    echo "Running processes: $(ps -e --no-headers | wc -l)"
    echo "=================================================================="
    echo "End of report"
}

generate_report() {
    print_header "GENERATE SYSTEM HEALTH REPORT"
    local dir file
    prompt dir "Directory to save the report [default: $DEFAULT_REPORT_DIR]: "
    dir=${dir:-$DEFAULT_REPORT_DIR}
    dir=${dir/#\~/$HOME}                       # expand a leading ~

    if [[ -e $dir && ! -d $dir ]]; then
        error "'$dir' exists but is not a directory."
        log_activity ERROR "Report failed: '$dir' is not a directory"
        return
    fi
    if [[ ! -d $dir ]]; then
        if ! confirm "Directory '$dir' does not exist. Create it?"; then
            info "Report generation cancelled."
            return
        fi
        if ! mkdir -p "$dir" 2>/dev/null; then
            error "Cannot create directory '$dir' (permission denied?)."
            log_activity ERROR "Report failed: cannot create '$dir'"
            return
        fi
    fi
    if [[ ! -w $dir || ! -x $dir ]]; then
        error "No write permission for directory '$dir'."
        log_activity ERROR "Report failed: no write permission for '$dir'"
        return
    fi

    info "Collecting system data..."
    collect_cpu_stats    || CPU_USAGE="unavailable"
    collect_memory_stats || MEM_PERCENT="unavailable"

    file="$dir/health_report_$(date '+%Y%m%d_%H%M%S').txt"
    if ! build_report > "$file" 2>/dev/null; then
        error "Failed to write report to '$file'."
        log_activity ERROR "Report failed: write error for '$file'"
        return
    fi

    info "Report saved to: $file"
    log_activity REPORT "Health report generated: $file"
    if confirm "View the report now?"; then
        cat "$file"
    fi
}

view_activity_log() {
    print_header "ACTIVITY HISTORY"
    if [[ ! -s $LOG_FILE ]]; then
        info "The activity log is empty."
        return
    fi
    if [[ ! -r $LOG_FILE ]]; then
        error "Cannot read log file '$LOG_FILE'."
        return
    fi
    local filter
    prompt filter "Filter by keyword, e.g. WARNING (Enter shows everything): "
    if [[ -n $filter ]]; then
        grep -iF -- "$filter" "$LOG_FILE" || info "No log entries contain '$filter'."
    else
        cat "$LOG_FILE"
    fi
    info "Log file: $LOG_FILE ($(wc -l < "$LOG_FILE") entries)"
}

clear_activity_log() {
    print_header "CLEAR ACTIVITY LOG"
    if [[ ! -s $LOG_FILE ]]; then
        info "The activity log is already empty."
        return
    fi
    if confirm "This will permanently delete $(wc -l < "$LOG_FILE") log entries. Continue?"; then
        if : > "$LOG_FILE" 2>/dev/null; then
            info "Activity log cleared."
            log_activity INFO "Activity log cleared by user"
        else
            error "Cannot clear '$LOG_FILE' (permission denied?)."
        fi
    else
        info "Clear cancelled. Log kept."
    fi
}

exit_application() {
    log_activity INFO "Session ended"
    info "Goodbye!"
    exit 0
}

# init_logging -> makes sure the log can be written, else falls back to /tmp
init_logging() {
    if ! mkdir -p "$LOG_DIR" 2>/dev/null || ! touch "$LOG_FILE" 2>/dev/null; then
        local fallback="${TMPDIR:-/tmp}/system_monitor_$(id -un).log"
        warn "Cannot write to '$LOG_FILE'. Using '$fallback' instead."
        LOG_FILE=$fallback
    fi
}

# check_dependencies -> warns at start-up about missing tools (does not exit)
check_dependencies() {
    local cmd missing=()
    for cmd in ps df awk sort head grep date uname; do
        has_cmd "$cmd" || missing+=("$cmd")
    done
    if (( ${#missing[@]} > 0 )); then
        warn "Some utilities are missing: ${missing[*]}. Related features will be unavailable."
        log_activity WARNING "Missing utilities at start-up: ${missing[*]}"
    fi
}

show_menu() {
    print_header "SYSTEM HEALTH & RESOURCE MONITOR"
    echo " 1) Display system information"
    echo " 2) Monitor CPU utilisation          (threshold ${CPU_THRESHOLD}%)"
    echo " 3) Monitor memory utilisation       (threshold ${MEM_THRESHOLD}%)"
    echo " 4) Monitor disk utilisation         (threshold ${DISK_THRESHOLD}%)"
    echo " 5) View and search running processes"
    echo " 6) Terminate a process"
    echo " 7) Generate a system health report"
    echo " 8) View activity history"
    echo " 9) Clear activity log"
    echo " 0) Exit"
}

# ----------------------------------------------------------------------------
# 6. Main
# ----------------------------------------------------------------------------
main() {
    trap 'printf "\n"; log_activity INFO "Session interrupted (Ctrl+C)"; exit 130' INT

    init_logging
    log_activity INFO "Session started"
    check_dependencies

    local choice
    while true; do
        show_menu
        prompt choice "Select an option [0-9]: "
        case $choice in
            1) display_system_info ;;
            2) monitor_cpu ;;
            3) monitor_memory ;;
            4) monitor_disk ;;
            5) view_processes ;;
            6) terminate_process ;;
            7) generate_report ;;
            8) view_activity_log ;;
            9) clear_activity_log ;;
            0) exit_application ;;
            *) error "Invalid selection '$choice'. Enter a number from 0 to 9."
               log_activity ERROR "Invalid menu selection: '$choice'"
               continue ;;
        esac
        pause
    done
}

main "$@"
