#!/bin/sh

APP_NAME=${MIIPC_APP_NAME:-APP}
MPP_NAME=${MIIPC_MPP_NAME:-mpp}
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LOG_DIR=${MIIPC_LOG_DIR:-/tmp}
APP_LOG=${MIIPC_LOG:-$LOG_DIR/miipc.log}
PID_FILE=${MIIPC_PID_FILE:-/tmp/miipc.pid}
MPP_READY_FILE=${MIIPC_MPP_READY_FILE:-/tmp/mppd}

export TZ=${TZ:-CST-8}
export LD_LIBRARY_PATH="$APP_DIR:$APP_DIR/lib:$APP_DIR/../lib:/sdk/lib:/lib:$LD_LIBRARY_PATH"

pid_by_name()
{
    local pid

    for pid in $(pidof "$1" 2>/dev/null); do
        if pid_is_name "$pid" "$1"; then
            echo "$pid"
            return 0
        fi
    done
    return 1
}

is_alive()
{
    [ -n "$1" ] && kill -0 "$1" 2>/dev/null
}

is_pid_number()
{
    case "$1" in
        ""|*[!0-9]*)
            return 1
            ;;
    esac
    return 0
}

is_safe_pid()
{
    is_pid_number "$1" || return 1
    [ "$1" -gt 1 ] || return 1
    return 0
}

pid_is_zombie()
{
    local pid=$1
    local stat_line
    local rest
    local state

    is_safe_pid "$pid" || return 1
    [ -r "/proc/$pid/stat" ] || return 1
    read stat_line <"/proc/$pid/stat" || return 1
    rest=${stat_line##*) }
    state=${rest%% *}
    [ "$state" = "Z" ]
}

pid_is_name()
{
    local pid=$1
    local name=$2
    local comm

    is_safe_pid "$pid" || return 1
    pid_is_zombie "$pid" && return 1
    [ -r "/proc/$pid/comm" ] || return 1
    read comm <"/proc/$pid/comm" || return 1
    [ "$comm" = "$name" ]
}

pid_from_file()
{
    local file=$1
    local name=$2
    local pid

    if [ ! -f "$file" ]; then
        return 1
    fi
    pid=$(cat "$file" 2>/dev/null)
    if pid_is_name "$pid" "$name"; then
        echo "$pid"
        return 0
    fi
    rm -f "$file"
    return 1
}

app_pid()
{
    local pid

    if [ -f "$PID_FILE" ]; then
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if pid_is_name "$pid" "$APP_NAME"; then
            echo "$pid"
            return
        fi
        rm -f "$PID_FILE"
    fi
    pid_by_name "$APP_NAME"
}

check_files()
{
    if [ ! -x "$APP_DIR/$APP_NAME" ]; then
        echo "$APP_NAME not executable: $APP_DIR/$APP_NAME"
        return 1
    fi
    return 0
}

check_mpp_file()
{
    if [ ! -x "$APP_DIR/$MPP_NAME" ]; then
        echo "$MPP_NAME not executable: $APP_DIR/$MPP_NAME"
        return 1
    fi
    return 0
}

start_mpp()
{
    local pid
    local elapsed

    check_mpp_file || return 1
    pid=$(pid_from_file "$MPP_READY_FILE" "$MPP_NAME")
    if is_alive "$pid"; then
        echo "$MPP_NAME already running pid=$pid"
        return 0
    fi
    pid=$(pid_by_name "$MPP_NAME")
    if is_alive "$pid"; then
        echo "$MPP_NAME already running pid=$pid without ready file"
        echo "$pid" >"$MPP_READY_FILE"
        return 0
    fi

    mkdir -p "$LOG_DIR"
    cd "$APP_DIR" || return 1
    echo "$MPP_NAME launch time=$(date '+%Y-%m-%d %H:%M:%S')" >>"$LOG_DIR/mpp.log"
    nohup "./$MPP_NAME" >>"$LOG_DIR/mpp.log" 2>&1 &
    pid=$!
    elapsed=0
    while [ "$elapsed" -lt 20 ]; do
        if ! is_alive "$pid"; then
            break
        fi
        if pid_from_file "$MPP_READY_FILE" "$MPP_NAME" >/dev/null 2>&1; then
            echo "$MPP_NAME started pid=$pid log=$LOG_DIR/mpp.log"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    echo "$MPP_NAME start failed, log=$LOG_DIR/mpp.log"
    return 1
}

start_app()
{
    local pid

    check_files || return 1
    pid=$(app_pid)
    if is_alive "$pid"; then
        echo "$APP_NAME already running pid=$pid"
        return 0
    fi

    rm -f "$PID_FILE"
    mkdir -p "$LOG_DIR"
    cd "$APP_DIR" || return 1
    echo "$APP_NAME launch time=$(date '+%Y-%m-%d %H:%M:%S')" >>"$APP_LOG"
    nohup "./$APP_NAME" >>"$APP_LOG" 2>&1 &
    pid=$!
    echo "$pid" >"$PID_FILE"
    (
        while is_alive "$pid"; do
            sleep 2
        done
        echo "$APP_NAME pid=$pid exited time=$(date '+%Y-%m-%d %H:%M:%S')" >>"$APP_LOG"
    ) &
    sleep 1

    if is_alive "$pid"; then
        echo "$APP_NAME started pid=$pid log=$APP_LOG"
        return 0
    fi

    echo "$APP_NAME start failed, log=$APP_LOG"
    rm -f "$PID_FILE"
    return 1
}

stop_one()
{
    local pid=$1

    if [ -n "$pid" ] && ! is_safe_pid "$pid"; then
        echo "skip unsafe pid=$pid"
        return 0
    fi
    if ! is_alive "$pid"; then
        return 0
    fi
    kill "$pid" 2>/dev/null
    sleep 1
    if is_alive "$pid"; then
        kill -9 "$pid" 2>/dev/null
    fi
}

stop_app()
{
    local pid

    pid=$(app_pid)
    stop_one "$pid"

    rm -f "$PID_FILE"
    echo "$APP_NAME stopped"
}

stop_mpp()
{
    local mpp_pid

    mpp_pid=$(pid_from_file "$MPP_READY_FILE" "$MPP_NAME")
    stop_one "$mpp_pid"
    for mpp_pid in $(pidof "$MPP_NAME" 2>/dev/null); do
        if pid_is_name "$mpp_pid" "$MPP_NAME"; then
            stop_one "$mpp_pid"
        fi
    done

    rm -f "$MPP_READY_FILE"
    echo "$MPP_NAME stopped"
}

status_app()
{
    local pid
    local mpp_pid

    pid=$(app_pid)
    if is_alive "$pid"; then
        echo "$APP_NAME running pid=$pid"
    else
        echo "$APP_NAME stopped"
    fi

    mpp_pid=$(pid_from_file "$MPP_READY_FILE" "$MPP_NAME")
    if [ -n "$mpp_pid" ]; then
        echo "$MPP_NAME running pid=$mpp_pid"
    else
        mpp_pid=$(pid_by_name "$MPP_NAME")
        if pid_is_name "$mpp_pid" "$MPP_NAME"; then
            echo "$MPP_NAME running pid=$mpp_pid without ready file"
        else
            echo "$MPP_NAME stopped"
        fi
    fi
}

case "$1" in
    ""|start)
        start_app
        ;;
    start-mpp)
        start_mpp
        ;;
    stop)
        stop_app
        ;;
    stop-mpp)
        stop_mpp
        ;;
    restart)
        stop_app
        start_app
        ;;
    status)
        status_app
        ;;
    *)
        echo "usage: $0 {start|start-mpp|stop|restart|status|stop-mpp}"
        exit 1
        ;;
esac
