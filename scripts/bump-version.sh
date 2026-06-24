#!/usr/bin/env bash
#
# bump-version.sh — bump the C-Static version in CMakeLists.txt and rotate
# the CHANGELOG.md [Unreleased] section.
#
# Usage: ./scripts/bump-version.sh {major|minor|patch|X.Y.Z}
#
# Does NOT commit or tag. Prints next-step reminders so the human stays in
# the loop on commit messages and CHANGELOG content.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CMAKE_FILE="$REPO_ROOT/CMakeLists.txt"
CHANGELOG="$REPO_ROOT/CHANGELOG.md"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

die() {
    echo "error: $*" >&2
    exit 1
}

usage() {
    cat >&2 <<EOF
Usage: $(basename "$0") {major|minor|patch|X.Y.Z}

Bumps the C-Static version in CMakeLists.txt and rotates CHANGELOG.md.

Examples:
  $(basename "$0") patch        # 0.3.0 -> 0.3.1
  $(basename "$0") minor        # 0.3.0 -> 0.4.0
  $(basename "$0") major        # 0.3.0 -> 1.0.0
  $(basename "$0") 1.2.3        # 0.3.0 -> 1.2.3 (explicit)

This script does NOT commit or tag. Review the changes, then:
  git add CMakeLists.txt CHANGELOG.md
  git commit -m "Release vX.Y.Z"
  git tag vX.Y.Z
  git push origin main vX.Y.Z
EOF
    exit 1
}

# ---------------------------------------------------------------------------
# Read current version from CMakeLists.txt
# ---------------------------------------------------------------------------

read_current_version() {
    local line
    line="$(grep -m1 '^project(cstatic VERSION' "$CMAKE_FILE")" \
        || die "could not find project(VERSION ...) line in $CMAKE_FILE"
    echo "$line" | sed -nE 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p'
}

# ---------------------------------------------------------------------------
# Compute next version
# ---------------------------------------------------------------------------

compute_next_version() {
    local current="$1" bump_type="$2"
    local major minor patch

    IFS='.' read -r major minor patch <<<"$current"

    case "$bump_type" in
        major)
            major=$((major + 1))
            minor=0
            patch=0
            ;;
        minor)
            minor=$((minor + 1))
            patch=0
            ;;
        patch)
            patch=$((patch + 1))
            ;;
        *)
            # Explicit X.Y.Z — validate format
            if [[ ! "$bump_type" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
                die "invalid version or bump type: '$bump_type' (expected major|minor|patch|X.Y.Z)"
            fi
            IFS='.' read -r major minor patch <<<"$bump_type"
            ;;
    esac

    echo "${major}.${minor}.${patch}"
}

# ---------------------------------------------------------------------------
# Update CMakeLists.txt (portable — no sed -i platform quirks)
# ---------------------------------------------------------------------------

update_cmake_version() {
    local old="$1" new="$2"
    local tmp
    tmp="$(mktemp)"
    # Use a delimiter unlikely to appear in the line
    sed "s|^project(cstatic VERSION ${old} |project(cstatic VERSION ${new} |" "$CMAKE_FILE" > "$tmp"
    mv "$tmp" "$CMAKE_FILE"
}

# ---------------------------------------------------------------------------
# Rotate CHANGELOG.md [Unreleased] -> [X.Y.Z] - YYYY-MM-DD
# ---------------------------------------------------------------------------

update_changelog() {
    local new_version="$1" today
    today="$(date +%Y-%m-%d)"

    if ! grep -q '^## \[Unreleased\]' "$CHANGELOG"; then
        die "could not find '## [Unreleased]' heading in $CHANGELOG — add it before bumping"
    fi

    local tmp
    tmp="$(mktemp)"

    # Replace the first ## [Unreleased] with the dated release header,
    # and insert a fresh ## [Unreleased] section above it.
    awk -v ver="$new_version" -v date="$today" '
        !found && /^## \[Unreleased\][[:space:]]*$/ {
            print "## [Unreleased]"
            print ""
            printf "## [%s] - %s\n", ver, date
            found = 1
            next
        }
        { print }
    ' "$CHANGELOG" > "$tmp"
    mv "$tmp" "$CHANGELOG"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

[ $# -eq 1 ] || usage
BUMP_TYPE="$1"

CURRENT="$(read_current_version)"
[ -n "$CURRENT" ] || die "could not parse current version from $CMAKE_FILE"

NEW="$(compute_next_version "$CURRENT" "$BUMP_TYPE")"

echo "Bumping version: $CURRENT -> $NEW"
echo

# 1. CMakeLists.txt
update_cmake_version "$CURRENT" "$NEW"

# Verify it landed
if ! grep -q "^project(cstatic VERSION ${NEW} " "$CMAKE_FILE"; then
    die "version update verification failed — $CMAKE_FILE does not contain VERSION $NEW"
fi
echo "  updated  CMakeLists.txt"

# 2. CHANGELOG.md
update_changelog "$NEW"
echo "  updated  CHANGELOG.md"
echo

# 3. Next steps
cat <<EOF
Version bumped to ${NEW}. Review the changes, then:

  git add CMakeLists.txt CHANGELOG.md
  git commit -m "Release v${NEW}"
  git tag v${NEW}
  git push origin main v${NEW}

Pushing the tag triggers the release workflow (builds binaries + creates
the GitHub release).
EOF
