#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Build the stock-distribution test images and run the full build/install test
# in each one. Only the distribution's default kernel is tested here.
#
#   docker/run-tests.sh [debian|ubuntu ...]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
variants=("$@")
if [ ${#variants[@]} -eq 0 ]; then
	variants=(debian ubuntu)
fi

for variant in "${variants[@]}"; do
	image="trigger5-test:$variant"
	dockerfile="$root/docker/Dockerfile.$variant"

	if [ ! -f "$dockerfile" ]; then
		echo "unknown variant: $variant" >&2
		exit 2
	fi

	echo
	echo "== $variant: building image"
	docker build -q -t "$image" -f "$dockerfile" "$root"

	vol="trigger5-work-$variant"

	echo "== $variant: running tests"
	docker volume create "$vol" >/dev/null
	docker run --rm \
		-v "$root":/src:ro \
		-v "$vol":/work \
		"$image" /src/ci/container-test.sh /src
	docker volume rm "$vol" >/dev/null
done
