#!/usr/bin/env bash
# Launch jscope under gdb so a crash leaves a BACKTRACE instead of a window that vanished.
#
# This is what the desktop launcher runs. gdb sits above the process doing nothing until something
# goes wrong; on an abnormal exit it dumps every thread's stack into the session log, and the log is
# renamed CRASH-<stamp>.log so it stands out from the ordinary ones. Launched from a desktop entry
# there is no terminal to print to, so without this a crash leaves nothing at all to work from.
#
# The app's own JLOGC output goes into the same file, ahead of the trace — which is usually the half
# that says what it was doing, and for this application that means which device it was opening.
# stdbuf keeps that output line-buffered so the last lines before the fault are actually on disk
# rather than sitting in a block buffer that dies with the process.
#
#   JSCOPE_BIN=/path/to/other  — override the binary
#   JSCOPE_LOG_DIR=/path       — override where logs are kept
#   JSCOPE_NO_GDB=1            — launch bare, no debugger
#
# Modelled on jayecu/apps/studio-jf/tools/studio-launch.sh, which solved the same problem.

set -u

BIN="${JSCOPE_BIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/jscope}"
LOG_DIR="${JSCOPE_LOG_DIR:-$HOME/.local/share/jscope/logs}"
KEEP=10                          # session logs to keep; CRASH-*.log are never pruned

[ -x "$BIN" ] || { echo "jscope-launch: no binary at $BIN" >&2; exit 127; }

# No gdb (or explicitly not wanted): the app still has to start. A missing debugger is a worse log,
# never a launcher that does nothing.
if [ -n "${JSCOPE_NO_GDB:-}" ] || ! command -v gdb >/dev/null 2>&1; then
    exec "$BIN" "$@"
fi

mkdir -p "$LOG_DIR" || exec "$BIN" "$@"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"     # pid too: two launches can share a second
LOG="$LOG_DIR/jscope-$STAMP.log"
MARK="===== ABNORMAL EXIT — thread backtraces follow ====="

GDB_CMDS=$(mktemp /tmp/jscope-gdb.XXXXXX)
trap 'rm -f "$GDB_CMDS"' EXIT
cat > "$GDB_CMDS" <<EOF
set pagination off
set confirm off
# Never reach for the network mid-crash, and keep its banner out of the log.
set debuginfod enabled off
set print thread-events off
# A GUI app takes these in normal operation; stopping on them would freeze it under the debugger.
# SIGUSR1 matters here specifically: JAppWindow uses it to request a screenshot, so a debugger that
# stopped on it would wedge the app every time a capture was taken.
handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
# Being asked to quit is not a crash. Left at gdb's default these STOP the inferior, which looks
# exactly like a fault from the script's side, so a logout -- or anything that pkills the app --
# produced a CRASH log and a "JScope crashed" notification. Passed through, the process dies of the
# signal and gdb records it in \$_exitsignal, which is what tells the two cases apart below.
handle SIGTERM nostop noprint pass
handle SIGINT  nostop noprint pass
handle SIGHUP  nostop noprint pass
run
# Three outcomes, and only the third is a crash:
#   exited normally      -> \$_exitcode set
#   killed by a signal   -> \$_exitcode void, \$_exitsignal set   (the pass-through ones above)
#   stopped by a fault   -> both void, because the process is still alive under the debugger
if \$_isvoid(\$_exitcode) && \$_isvoid(\$_exitsignal)
  echo \n$MARK\n
  info program
  echo \n--- all threads, full frames ---\n
  thread apply all bt full
  echo \n--- registers (crashing thread) ---\n
  info registers
end
quit
EOF

# stdbuf's LD_PRELOAD is inherited by the inferior, so the app's own stderr is line-buffered too.
stdbuf -oL -eL gdb -q -batch -x "$GDB_CMDS" --args "$BIN" "$@" > "$LOG" 2>&1
status=$?

if grep -q "$MARK" "$LOG" 2>/dev/null; then
    CRASH="$LOG_DIR/CRASH-$STAMP.log"
    mv "$LOG" "$CRASH"
    echo "jscope-launch: crashed — backtrace in $CRASH" >&2
    status=1                     # gdb's own exit code says nothing about the inferior's fate
    # Say so on the desktop as well: nothing else is watching this stderr.
    command -v notify-send >/dev/null 2>&1 &&
        notify-send -u critical "JScope crashed" "Backtrace: $CRASH"
else
    # Ordinary session: keep the newest few for context, drop the rest. Crash logs are never pruned.
    ls -1t "$LOG_DIR"/jscope-*.log 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r old; do
        rm -f "$old"
    done
fi

exit $status
