#!/usr/bin/env bash
# Runs inside the container: one or more suites, the web page, the unit tests or a shell.
set -u

# The harness mounts its own sysfs inside its sandbox; the kernel refuses that
# while the container's /sys is read-only.
mount -o remount,rw /sys 2>/dev/null || true

# The recorder logs through syslog (/dev/log), which a container does not have. The harness's sandbox binds its own log
# socket over /dev/log only when something is there, so without this the recorder's own log is lost (no syslog.log).
[ -e /dev/log ] || touch /dev/log 2>/dev/null || true

# Run folders stay on the container's own disk and are copied to the host's folder.
# The recorder's databases are left out: a folder shared with the host is slow for
# them, and they are deleted after a passing run anyway.
export REVERE_HARNESS_OUT=/work/out
mkdir -p /work/out /out
sync_out() {
    rsync -a --exclude 'recorder/data/' --exclude 'recorder/secret/' --exclude 'recorder/home/' \
          --exclude 'app/data/' --exclude 'app/secret/' --exclude 'app/home/' /work/out/ /out/ 2>/dev/null || true
    # On a Linux host the copies would belong to root; give them to the user who started the run.
    if [ -n "${HOST_UID:-}" ] && [ "${HOST_UID}" != 0 ]; then
        chown -R "${HOST_UID}:${HOST_GID:-$HOST_UID}" /out 2>/dev/null || true
    fi
}
# Earlier runs' reports, so the web page lists them too.
rsync -a /out/ /work/out/ 2>/dev/null || true
( while true; do sleep 5; sync_out; done ) &
sync_pid=$!
trap 'kill "$sync_pid" 2>/dev/null; sync_out' EXIT
trap 'exit 130' INT TERM

usage() {
    cat <<'TEXT'
usage: <suite>... [-- <arguments for every suite>] | web | test | shell | help
  smoke recording            run these suites in turn
  stress -- --param minutes=1
  web                        the control page (the host script publishes its port)
  test                       the harness's unit tests
  shell                      a shell in the container
TEXT
}

case "${1:-help}" in
    help|-h|--help) usage; exit 0 ;;
    shell) bash; exit $? ;;
    test) cd /opt/harness && python3 -m pytest -q -p no:cacheprovider tests; exit $? ;;
    web)
        /opt/harness/run.sh web --bind 0.0.0.0 --port "${HARNESS_PORT:-8090}" --revere-dir /opt/revere
        exit $? ;;
esac

suites=()
extra=()
seen_dashes=0
for arg in "$@"; do
    if [ "$seen_dashes" = 1 ]; then extra+=("$arg")
    elif [ "$arg" = "--" ]; then seen_dashes=1
    else suites+=("$arg"); fi
done

worst=0
summary=()
for suite in "${suites[@]}"; do
    /opt/harness/run.sh "$suite" --revere-dir /opt/revere ${extra[@]+"${extra[@]}"}
    code=$?
    summary+=("$suite: exit $code")
    [ "$code" -gt "$worst" ] && worst=$code
done
printf '%s\n' "${summary[@]}"
exit "$worst"
