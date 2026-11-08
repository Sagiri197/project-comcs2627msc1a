#!/usr/bin/env bash
# Build the alert server on the Pi, install it as /usr/local/bin/comcs-alert-server and
# restart the service if it is running. Run as the user who owns the checkout (uses sudo to
# install), or as root when root owns the checkout.
#
# Usage: server/scripts/build.sh [--test] [--no-install]
#   --test        also build and run the unit tests (slower)
#   --no-install  only build
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

testing=OFF
install=1
for arg in "$@"; do
    case "$arg" in
        --test)       testing=ON ;;
        --no-install) install=0 ;;
        *)            die "unknown option '$arg'" ;;
    esac
done

# Run as the user who owns the checkout, so the build dir and submodules stay owned by them.
owner="$(stat -c %U "$REPO_DIR")"
if [[ $EUID -eq 0 && "$owner" != root ]]; then
    die "the checkout belongs to $owner: run as $owner, not root (the build dir must stay $owner-owned)"
fi

# sudo only when not already root (a root shell may not have sudo installed).
as_root() { if [[ $EUID -eq 0 ]]; then "$@"; else sudo "$@"; fi; }

build_dir="$REPO_DIR/server/build"

log "Updating submodules"
git -C "$REPO_DIR" submodule update --init --recursive

log "Configuring (BUILD_TESTING=$testing)"
cmake -S "$REPO_DIR/server" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$testing"

log "Building with $(nproc) jobs"
cmake --build "$build_dir" --parallel "$(nproc)"

if [[ $testing == ON ]]; then
    log "Running tests"
    ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
fi

if (( install )); then
    log "Installing $SERVER_BIN"
    as_root install -m 0755 "$build_dir/bin/alert_server" "$SERVER_BIN"
    if systemctl is-active --quiet "$SERVICE"; then
        log "Restarting $SERVICE"
        as_root systemctl restart "$SERVICE"
    fi
fi
