#!/bin/bash
# Copy the built program (./build.sh) and the sites to the Steam Machine and install the user
# service the TV shell starts them with. A running site is restarted, so it runs the new files.
#
# Usage: ./deploy.sh
#        ./deploy.sh --log <site>   follow a site's log, e.g. --log youtube
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}
# Where it lives on the Steam Machine, relative to the home directory.
FOLDER=${FOLDER:-apps/webapp}

if [ "${1:-}" = --log ]; then
    exec ssh "$TARGET" journalctl --user -u "webapp@${2:?which site}.service" -f -o cat
fi

ssh "$TARGET" "mkdir -p '$FOLDER'"
rsync -a "$HERE/build/webapp" "$TARGET:$FOLDER/"
rsync -a --delete "$HERE/sites/" "$TARGET:$FOLDER/sites/"
ssh "$TARGET" "
    missing=\$(ldd '$FOLDER/webapp' | grep 'not found' | sort -u)
    if [ -n \"\$missing\" ]; then
        echo 'missing libraries:' >&2
        echo \"\$missing\" >&2
        exit 1
    fi
"

# YouTube was the shell's own page until it became a site: its sign-in moves along, once,
# and only while nothing has it open (the old shell).
ssh "$TARGET" '
    old=.local/share/tvshell/QtWebEngine/youtube
    new=.local/share/webapp/youtube
    if [ -d "$old" ] && [ ! -e "$new" ]; then
        if ls -l /proc/[0-9]*/fd 2>/dev/null | grep -q "$HOME/$old"; then
            echo "YouTube profile still open in $old (the old shell?): deploy the shell first" >&2
            exit 1
        fi
        mkdir -p "$(dirname "$new")" && mv "$old" "$new" && echo "moved YouTube profile to ~/$new"
    fi
'

# Installed, not enabled: the shell starts a site from its tile. Running sites restart.
rsync -a "$HERE/webapp@.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" '
    systemctl --user daemon-reload
    for unit in $(systemctl --user list-units --plain --no-legend "webapp@*.service" | cut -d" " -f1); do
        systemctl --user restart "$unit" && echo "restarted $unit"
    done
    echo "installed"
'
