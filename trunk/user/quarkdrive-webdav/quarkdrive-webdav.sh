#!/bin/sh
# Padavan service control. Credentials are data, never shell command text.
NAME=quarkdrive-webdav
RUNDIR=/tmp/quarkdrive-webdav
umask 077

fail() { logger -t "$NAME" "$1"; printf '%s\n' "$1" >&2; return 1; }
get() { nvram get "qd_$1"; }
number() {
    case "$2" in ''|*[!0-9]*) fail "Invalid number: $1"; return 1 ;; esac
    [ "${#2}" -le 10 ] && [ "$2" -ge "$3" ] && [ "$2" -le "$4" ] 2>/dev/null || {
        fail "Out of range: $1"; return 1;
    }
}
running() {
    PID=$(cat "$RUNDIR/pid" 2>/dev/null)
    case "$PID" in ''|*[!0-9]*|0|1) return 1 ;; esac
    kill -0 "$PID" 2>/dev/null || return 1
    expected=$(cat "$RUNDIR/exe" 2>/dev/null)
    actual=$(readlink "/proc/$PID/exe" 2>/dev/null)
    [ -n "$expected" ] && [ "${actual% (deleted)}" = "$expected" ]
}
check_config() {
    binary=$(get binary)
    case "$binary" in /*) ;; *) fail 'Binary path must be absolute'; return 1 ;; esac
    [ -f "$binary" ] && [ -x "$binary" ] || { fail 'Binary is missing or not executable'; return 1; }
    binary=$(readlink -f "$binary") || return 1
    cookie=$(get cookie)
    [ -n "$cookie" ] && [ "${#cookie}" -le 4095 ] || { fail 'Cookie is missing or exceeds 4095 bytes'; return 1; }
    auth_user=$(get auth_user)
    auth_password=$(get auth_password)
    if { [ -n "$auth_user" ] && [ -z "$auth_password" ]; } ||
       { [ -z "$auth_user" ] && [ -n "$auth_password" ]; }; then
        fail 'WebDAV username and password must be specified together'; return 1
    fi
    host=$(get host)
    case "$host" in ''|*[!0-9a-fA-F:.]*) fail 'Listen address must be an IPv4 or IPv6 address'; return 1 ;; esac
    root=$(get root)
    case "$root" in /*) ;; *) fail 'Drive root must start with /'; return 1 ;; esac
    prefix=$(get strip_prefix)
    case "$prefix" in ''|/*) ;; *) fail 'URL prefix must start with /'; return 1 ;; esac
    port=$(get port); number port "$port" 1 65535 || return 1
    read_buffer=$(get read_buffer_size); number read_buffer_size "$read_buffer" 1 16777216 || return 1
    upload_buffer=$(get upload_buffer_size); number upload_buffer_size "$upload_buffer" 1 16777216 || return 1
    cache_size=$(get cache_size); number cache_size "$cache_size" 0 100000 || return 1
    cache_ttl=$(get cache_ttl); number cache_ttl "$cache_ttl" 0 2147483647 || return 1
    refresh=$(get refresh_cache_secs_interval); number refresh_cache_secs_interval "$refresh" 1 2147483647 || return 1
    upload_wait=$(get upload_wait_timeout); number upload_wait_timeout "$upload_wait" 0 2147483647 || return 1
    for key in auto_index no_trash read_only skip_upload_same_size prefer_http_download redirect debug; do
        case "$(get "$key")" in 0|1) ;; *) fail "Invalid switch: $key"; return 1 ;; esac
    done
}
stop_service() {
    if running; then
        kill "$PID" 2>/dev/null || return 1
        count=0
        while running && [ "$count" -lt 5 ]; do sleep 1; count=$((count + 1)); done
        if running; then
            kill -9 "$PID" 2>/dev/null || return 1
            count=0
            while running && [ "$count" -lt 3 ]; do sleep 1; count=$((count + 1)); done
            running && { fail 'Process did not exit'; return 1; }
        fi
        logger -t "$NAME" 'Service stopped'
    fi
    # With no writer the log reader reaches EOF. Do not kill an unchecked PID.
    rm -f "$RUNDIR/pid" "$RUNDIR/exe" "$RUNDIR/eip93" "$RUNDIR/log"
}
launch() {
    running && return 0
    set -- --host "$host" -p "$port" --root "$root" --no-self-upgrade \
        --read-buffer-size "$read_buffer" --upload-buffer-size "$upload_buffer" \
        --cache-size "$cache_size" --cache-ttl "$cache_ttl" \
        --refresh-cache-secs-interval "$refresh" --upload-wait-timeout "$upload_wait"
    [ -z "$prefix" ] || set -- "$@" --strip-prefix "$prefix"
    for key in auto_index no_trash read_only skip_upload_same_size prefer_http_download redirect debug; do
        if [ "$(get "$key")" = 1 ]; then
            option=$(printf '%s' "$key" | tr '_' '-')
            set -- "$@" "--$option"
        fi
    done
    rm -f "$RUNDIR/log" "$RUNDIR/eip93"
    mkfifo "$RUNDIR/log" || return 1
    logger -t "$NAME" < "$RUNDIR/log" >/dev/null 2>&1 &
    reader=$!
    (
        export QUARK_COOKIE="$cookie" QUARK_EIP93_STATUS_FILE="$RUNDIR/eip93" NO_COLOR=1
        unset WEBDAV_AUTH_USER WEBDAV_AUTH_PASSWORD QUARK_EIP93
        [ -z "$auth_user" ] || export WEBDAV_AUTH_USER="$auth_user"
        [ -z "$auth_password" ] || export WEBDAV_AUTH_PASSWORD="$auth_password"
        export RUST_LOG='quarkdrive_webdav=info,reqwest=warn'
        [ "$(get debug)" != 1 ] || export RUST_LOG='quarkdrive_webdav=debug,reqwest=debug'
        exec "$binary" "$@"
    ) > "$RUNDIR/log" 2>&1 &
    PID=$!
    printf '%s\n' "$binary" > "$RUNDIR/exe"
    printf '%s\n' "$PID" > "$RUNDIR/pid"
    sleep 1
    if ! running; then
        wait "$PID" 2>/dev/null
        wait "$reader" 2>/dev/null
        rm -f "$RUNDIR/pid" "$RUNDIR/exe" "$RUNDIR/eip93" "$RUNDIR/log"
        fail 'Service failed to start; check the system log'; return 1
    fi
    logger -t "$NAME" 'Service started'
}

case "${1-}" in
    status) running; exit $? ;;
    check) check_config; exit $? ;;
    start|stop|restart|start_now|restart_now) ;;
    *) echo 'Usage: quarkdrive-webdav.sh {start|stop|restart|start_now|restart_now|status|check}' >&2; exit 1 ;;
esac
mkdir -p "$RUNDIR" || exit 1
chmod 700 "$RUNDIR" || exit 1
if [ -d "$RUNDIR/control.lock" ]; then
    owner=$(cat "$RUNDIR/control.lock/owner" 2>/dev/null)
    case "$owner" in
        ''|*[!0-9]*|0|1) ;;
        *) if ! kill -0 "$owner" 2>/dev/null; then
               rm -f "$RUNDIR/control.lock/owner"
               rmdir "$RUNDIR/control.lock" 2>/dev/null
           fi ;;
    esac
fi
if ! mkdir "$RUNDIR/control.lock" 2>/dev/null; then
    fail 'Service control is busy'; exit 1
fi
printf '%s\n' "$$" > "$RUNDIR/control.lock/owner"
trap 'rm -f "$RUNDIR/control.lock/owner"; rmdir "$RUNDIR/control.lock"' 0
trap 'exit 1' 1 2 15

case "$1" in
    stop) stop_service ;;
    start|restart)
        if [ "$(nvram get quarkdrive_enable)" != 1 ]; then
            stop_service
        else
            check_config || exit 1
            if [ "$1" = restart ]; then stop_service || exit 1; fi
            launch
        fi ;;
    start_now|restart_now)
        check_config || exit 1
        if [ "$1" = restart_now ]; then stop_service || exit 1; fi
        launch ;;
esac
