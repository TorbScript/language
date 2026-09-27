#!/bin/sh
# The nightly backup of the files below /srv/torb with restic (tools/deploy/compose.example.yml, service `backup`,
# started by the host's cron): the registry's index and archives, the synced downloads and the index mirror, to the
# restic repository RESTIC_REPOSITORY names - object storage at another provider than the server's
# (docs/design/RELEASE.md section 7.11). The database is left out: Litestream replicates it continuously, and a copy of
# a SQLite file taken while it is written is not a database.
#
#   docker compose --profile backup run --rm backup
#
# Keeps 14 daily, 8 weekly and 12 monthly snapshots, and reads a thirtieth of the stored data back every night, so
# the whole repository is verified once a month. Restoring is `restic restore latest --target /` from a shell of the
# same image (`docker compose --profile backup run --rm --entrypoint sh backup`).
#
# POSIX sh: the restic image is Alpine.

set -eu

if ! restic cat config >/dev/null 2>&1; then
  echo "backup.sh: no restic repository at $RESTIC_REPOSITORY yet - creating it"
  restic init
fi

restic backup --tag torb --exclude '*.part' \
  /srv/torb/registry /srv/torb/download /srv/torb/index-mirror

restic forget --tag torb --keep-daily 14 --keep-weekly 8 --keep-monthly 12 --prune

day=$(date +%d)
restic check --read-data-subset="$(( (day % 30) + 1 ))/30"

echo "backup.sh: done"
