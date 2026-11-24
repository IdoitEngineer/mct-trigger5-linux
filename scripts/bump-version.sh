#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Update every current-version reference in the tree.
#
#   scripts/bump-version.sh [--dry-run] X.Y.Z [message...]
#
# The detailed CHANGELOG.md is maintained by hand; this script only keeps the
# driver, DKMS and Debian version strings consistent and prepends a Debian
# changelog entry.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
version_file="$root/VERSION"
dkms_conf="$root/linux-trigger5/dkms.conf"
driver="$root/linux-trigger5/trigger5.c"
debian="$root/linux-trigger5/debian"

usage()
{
	echo "usage: $0 [--dry-run] X.Y.Z [message...]" >&2
	exit 2
}

dry_run=0
if [ "${1:-}" = "--dry-run" ]; then
	dry_run=1
	shift
fi
[ $# -ge 1 ] || usage
new=$1
shift
message=$*

if ! printf '%s' "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
	echo "error: version must be major.minor.patch (got '$new')" >&2
	exit 1
fi

if [ ! -f "$version_file" ]; then
	echo "error: $version_file is missing" >&2
	exit 1
fi

old=$(tr -d '[:space:]' < "$version_file")
if [ "$old" = "$new" ]; then
	echo "already at $new"
	exit 0
fi
major=${new%%.*}
rest=${new#*.}
minor=${rest%%.*}
patch=${new##*.}

if [ "$dry_run" -eq 0 ] && ! git -C "$root" diff --quiet; then
	echo "warning: working tree has uncommitted changes; continuing" >&2
fi

update()
{
	local file=$1 expr=$2
	local tmp

	[ -f "$file" ] || { echo "error: missing $file" >&2; exit 1; }
	tmp=$(mktemp)
	sed -E "$expr" "$file" > "$tmp"
	if cmp -s "$file" "$tmp"; then
		rm -f "$tmp"
		return
	fi
	if [ "$dry_run" -eq 1 ]; then
		echo "would update ${file#"$root"/}"
		rm -f "$tmp"
	else
		chmod --reference="$file" "$tmp"
		mv "$tmp" "$file"
		echo "updated ${file#"$root"/}"
	fi
}

update "$version_file" "s/^[0-9]+\.[0-9]+\.[0-9]+$/$new/"
update "$dkms_conf" "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$new\"/"
update "$driver" "s/^#define DRIVER_MAJOR[[:space:]]+[0-9]+/#define DRIVER_MAJOR           $major/"
update "$driver" "s/^#define DRIVER_MINOR[[:space:]]+[0-9]+/#define DRIVER_MINOR           $minor/"
update "$driver" "s/^#define DRIVER_PATCHLEVEL[[:space:]]+[0-9]+/#define DRIVER_PATCHLEVEL      $patch/"
update "$debian/trigger5-dkms.install" "s#usr/src/trigger5-[0-9][0-9.]*#usr/src/trigger5-$new#g"
update "$debian/trigger5-dkms.postinst" "s/^VERSION=.*/VERSION=$new/"
update "$debian/trigger5-dkms.prerm" "s/^VERSION=.*/VERSION=$new/"
update "$debian/README.Debian" "s/trigger5-$old/trigger5-$new/g; s/trigger5-dkms_$old/trigger5-dkms_$new/g"
update "$root/README.md" "s/trigger5-dkms_$old/trigger5-dkms_$new/g; s/trigger5_${old}_amd64/trigger5_${new}_amd64/g; s#trigger5/$old#trigger5/$new#g"
update "$root/linux-trigger5/README.md" "s/trigger5-dkms_$old/trigger5-dkms_$new/g; s#trigger5/$old#trigger5/$new#g"
update "$root/AGENTS.md" "s/trigger5-dkms_$old/trigger5-dkms_$new/g"

changelog="$debian/changelog"
if head -n 1 "$changelog" | grep -q "($new)"; then
	echo "debian/changelog already has $new"
else
	name=$(git -C "$root" config user.name 2>/dev/null || true)
	email=$(git -C "$root" config user.email 2>/dev/null || true)
	[ -n "$name" ] || name="Jeringa"
	[ -n "$email" ] || email="jeringa@example.com"
	[ -n "$message" ] || message="Release $new."

	tmp=$(mktemp)
	{
		printf 'trigger5 (%s) unstable; urgency=medium\n\n' "$new"
		printf '%s\n' "$message" | while IFS= read -r line; do
			printf '  * %s\n' "$line"
		done
		printf '\n -- %s <%s>  %s\n\n' "$name" "$email" "$(date -R)"
		cat "$changelog"
	} > "$tmp"
	if [ "$dry_run" -eq 1 ]; then
		echo "would prepend a debian/changelog entry for $new"
		rm -f "$tmp"
	else
		mv "$tmp" "$changelog"
		echo "updated debian/changelog"
	fi
fi

if [ "$dry_run" -eq 0 ]; then
	got=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' "$dkms_conf")
	[ "$got" = "$new" ] || { echo "error: dkms.conf is $got" >&2; exit 1; }
	git -C "$root" diff --check
fi

echo
echo "Version is now $new."
echo "Next:"
echo "  1. update CHANGELOG.md (kept by hand)"
echo "  2. ./scripts/test-local.sh"
echo "  3. git add -A && git commit -m \"release: $new\""
echo "  4. git tag -a v$new -m \"trigger5 $new\""
