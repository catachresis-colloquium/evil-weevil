#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jonathan D.A. Jewell (metadatastician) <j.d.a.jewell@open.ac.uk>
#
# install-toolchain.sh — the three tools this repository cannot be checked without.
#
#   bash scripts/install-toolchain.sh [--check] [--prefix DIR] [--only TOOL ...]
#
#     --check    report what is present and missing; install nothing, exit 1 if
#                anything is missing. Use this in CI as a fast preflight, and on
#                a developer's machine to find out what `just seam-check` needs.
#     --prefix   where to install (default: ~/.cache/evil-weevil/toolchain).
#                CI passes a directory it caches; the default is a cache path
#                precisely so a local install is also a cache and never lands in
#                the repository.
#     --only     install and verify only TOOL (zig, idris2 or just); repeat it
#                for more than one. The default is all three. A job that needs
#                only `just` (the house gates in seam.yml) takes it from here, so
#                it gets the same pinned, SHA-256-checked binary and not a second
#                download written into a workflow.
#
# WHY A SCRIPT AND NOT A LIST OF APT PACKAGES
#
# Neither tool is available in a form that makes sense here:
#
#   * Idris2 publishes source only — no Linux binaries in any release, and no
#     conda/apt package. It must be bootstrapped with Chez Scheme. The v0.7.0
#     tarball ships PRE-GENERATED Scheme for the compiler itself, so the whole
#     bootstrap needs only Chez Scheme, make, gcc and GMP.
#   * Zig's own versions matter: this repository's build depends on 0.16.0
#     semantics. `zig = "latest"` in mise.toml was inherited from the template and
#     is exactly wrong for a project whose central claim is a frozen ABI — a new
#     Zig is free to change the build out from under the artefact.
#   * `just` is not a mise registry package either, and — measured against the
#     Ubuntu 24.04 GitHub runner image's own software list on 2026-09-20 — it is
#     not on ubuntu-latest. The seam gate is `just seam-check`, so a workflow that
#     assumes the runner has `just` fails at its first command; here it comes from
#     a pinned release, with the tarball's SHA-256 checked before extraction.
#
# Everything is pinned, idempotent and checked: running it twice is a no-op, and
# the versions are compared, not assumed. Numbers are the ones this repository was
# measured against (see DEVELOPMENT-PLAN.adoc and STATE.a2ml).
#
# Exit: 0 = both tools present at the pinned versions; 1 = missing and not
# installing (with --check), or an install step failed.

set -euo pipefail

ZIG_VERSION="0.16.0"
IDRIS2_VERSION="0.7.0"
JUST_VERSION="1.58.0"
# The digest of just-1.58.0-x86_64-unknown-linux-musl.tar.gz as published on
# 2026-08-03. It is committed here rather than fetched alongside the tarball: a
# checksum downloaded from the same place as the thing it checks proves only that
# the download was not corrupted on the way, which is worth something, but not
# what "verified" should mean. Bump both together, in one commit.
JUST_SHA256="4a5cc2f53e6f0f8c59092a6cc38291eb729d46a7dd95d3ae582008881b84931d"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PREFIX="${EE_TOOLCHAIN_PREFIX:-$HOME/.cache/evil-weevil/toolchain}"
CHECK_ONLY=0
# Which tools this run is about. All three unless --only narrows it.
WANT_ZIG=1
WANT_IDRIS2=1
WANT_JUST=1
ONLY_SEEN=0

usage() { echo "usage: $(basename "$0") [--check] [--prefix DIR] [--only zig|idris2|just ...]" >&2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK_ONLY=1 ;;
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --only)
            if [ "$ONLY_SEEN" -eq 0 ]; then WANT_ZIG=0; WANT_IDRIS2=0; WANT_JUST=0; ONLY_SEEN=1; fi
            case "${2:-}" in
                zig) WANT_ZIG=1 ;;
                idris2) WANT_IDRIS2=1 ;;
                just) WANT_JUST=1 ;;
                *) echo "--only needs one of: zig, idris2, just (got '${2:-}')" >&2; usage; exit 2 ;;
            esac
            shift ;;
        -h|--help) sed -n '4,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) usage; exit 2 ;;
    esac
    shift
done

say()  { printf '%s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# The installed version, or empty. Tolerant of tools that print to stderr.
zig_version() {
    have zig || return 0
    zig version 2>/dev/null | head -1
}
idris2_version() {
    have idris2 || return 0
    idris2 --version 2>/dev/null | sed -n 's/.*version \([0-9][0-9.]*\).*/\1/p' | head -1
}
just_version() {
    have just || return 0
    just --version 2>/dev/null | sed -n 's/^just \([0-9][0-9.]*\).*/\1/p' | head -1
}

ZIG_HAVE="$(zig_version)"
IDRIS2_HAVE="$(idris2_version)"
JUST_HAVE="$(just_version)"

# ── Verify, rather than announce ─────────────────────────────────────────────
# A toolchain script's job is that the tools WORK, not that they exist. A compiler
# that starts and cannot find its own base library is a failure no version string
# reveals — and it is the failure a relocated install produces.
#
# Two probes, because they fail independently and the first alone was not enough.
# The type-check probe needs only the LIBRARIES. Building an executable also needs
# the backend SUPPORT files (chez/support.ss, libidris2_support.so), and those can
# be missing while every library is present: from 2026-10-01 until this probe
# existed, CI's install passed the type-check probe, `just proof-check-idris2`
# passed, and `just seam-check` died at its first command building the ABI
# generator ("Can't find data file chez/support.ss"). The seam gate has to build
# and run a program, so the verification has to as well.
#
# Returns 0 if both pass, 1 if the libraries are unreachable, 2 if a program cannot
# be built or does not run correctly.
verify_idris2() {
    local probe rc=0
    probe="$(mktemp -d)"
    cat > "$probe/Probe.idr" <<'PROBE'
module Probe

import Data.Nat

%default total

theCompilersBaseLibraryIsReachable : LTE 3 5
theCompilersBaseLibraryIsReachable = LTESucc (LTESucc (LTESucc LTEZero))
PROBE
    cat > "$probe/Main.idr" <<'PROBE'
module Main

main : IO ()
main = putStrLn "ee-codegen-probe-ok"
PROBE
    if ! ( cd "$probe" && idris2 --check Probe.idr >/dev/null 2>&1 ); then
        rc=1
    elif [ "$( cd "$probe" && idris2 Main.idr -o probe >/dev/null 2>&1 \
               && ./build/exec/probe 2>/dev/null )" != "ee-codegen-probe-ok" ]; then
        # Checked by the program's OUTPUT, not by idris2's exit status: idris2
        # exits 0 after "INTERNAL ERROR: Can't find data file", having built
        # nothing (measured with 0.7.0).
        rc=2
    fi
    rm -rf "$probe"
    return "$rc"
}

# Why verify_idris2 failed, in words a person can act on.
explain_idris2_failure() {
    case "$1" in
        1) say "      idris2 cannot find its base libraries (a module importing Data.Nat"
           say "      does not type-check)." ;;
        2) say "      idris2 type-checks but cannot build and run a program: its backend"
           say "      support files (chez/support.ss, libidris2_support.so) are not where"
           say "      it looks. This is the half-installed layout this script produced"
           say "      before 2026-11-14; reinstalling with this script repairs it." ;;
    esac
}

say "evil-weevil toolchain"
[ "$WANT_ZIG" -eq 1 ]    && say "  zig     want $ZIG_VERSION  have ${ZIG_HAVE:-none}"
[ "$WANT_IDRIS2" -eq 1 ] && say "  idris2 want $IDRIS2_VERSION  have ${IDRIS2_HAVE:-none}"
[ "$WANT_JUST" -eq 1 ]   && say "  just    want $JUST_VERSION  have ${JUST_HAVE:-none}"

missing=0
[ "$WANT_ZIG" -eq 0 ]    || [ "$ZIG_HAVE" = "$ZIG_VERSION" ]       || missing=$((missing + 1))
[ "$WANT_IDRIS2" -eq 0 ] || [ "$IDRIS2_HAVE" = "$IDRIS2_VERSION" ] || missing=$((missing + 1))
[ "$WANT_JUST" -eq 0 ]   || [ "$JUST_HAVE" = "$JUST_VERSION" ]     || missing=$((missing + 1))

# An idris2 at the right version that fails verification is a broken install, not
# a missing one. --check reports it and fails; an install run rebuilds it, rather
# than exiting and leaving the broken copy for the next run to find again.
IDRIS2_BROKEN=0
if [ "$WANT_IDRIS2" -eq 1 ] && [ "$IDRIS2_HAVE" = "$IDRIS2_VERSION" ]; then
    verify_rc=0
    verify_idris2 || verify_rc=$?
    if [ "$verify_rc" -ne 0 ]; then
        say ""
        say "  idris2 $IDRIS2_HAVE at $(command -v idris2) FAILS verification:"
        explain_idris2_failure "$verify_rc"
        IDRIS2_BROKEN=1
        missing=$((missing + 1))
    fi
fi

if [ "$missing" -eq 0 ]; then
    say ""
    say "PASS: every requested tool is present at its pinned version."
    [ "$WANT_IDRIS2" -eq 1 ] && say "      idris2 type-checks a module importing Data.Nat AND builds and runs a program."
    [ "$WANT_ZIG" -eq 1 ]    && say "      zig:    $(command -v zig)"
    [ "$WANT_IDRIS2" -eq 1 ] && say "      idris2: $(command -v idris2)"
    [ "$WANT_JUST" -eq 1 ]   && say "      just:   $(command -v just)"
    exit 0
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
    say ""
    say "FAIL: $missing tool(s) missing, at the wrong version, or failing verification."
    say "      Run: bash scripts/install-toolchain.sh   (or --prefix DIR to choose where)"
    exit 1
fi

# ── just ─────────────────────────────────────────────────────────────────────
install_just() {
    say ""
    say "==> just $JUST_VERSION (the entry point to every gate)"
    local bin="$PREFIX/bin/just"
    local url="https://github.com/casey/just/releases/download/$JUST_VERSION/just-$JUST_VERSION-x86_64-unknown-linux-musl.tar.gz"
    if [ -x "$bin" ] && [ "$("$bin" --version 2>/dev/null | sed -n 's/^just \([0-9][0-9.]*\).*/\1/p')" = "$JUST_VERSION" ]; then
        say "    already installed at $bin"
        return 0
    fi
    mkdir -p "$PREFIX/bin"
    local tmp="$PREFIX/just-dl.tar.gz"
    say "    downloading $url"
    curl -fsSL -o "$tmp" "$url"
    if ! echo "$JUST_SHA256  $tmp" | sha256sum -c - >/dev/null 2>&1; then
        say "    FAIL: the tarball does not match the digest committed in this script."
        say "          Expected $JUST_SHA256"
        say "          Refusing to install it. If upstream really did re-release"
        say "          $JUST_VERSION, bump JUST_VERSION and JUST_SHA256 together."
        rm -f "$tmp"
        return 1
    fi
    # Only the binary is installed. The release also ships a man page and shell
    # completions; nothing here needs them, and a toolchain cache should hold what
    # the gates run, not a distribution directory.
    tar -xzf "$tmp" -C "$PREFIX/bin" just
    chmod +x "$bin"
    rm -f "$tmp"
    say "    installed $("$bin" --version) at $bin (sha256 verified)"
}

# ── Zig ──────────────────────────────────────────────────────────────────────
install_zig() {
    say ""
    say "==> zig $ZIG_VERSION"
    local dir="$PREFIX/zig-$ZIG_VERSION"
    local url="https://ziglang.org/download/$ZIG_VERSION/zig-x86_64-linux-$ZIG_VERSION.tar.xz"
    # The upstream tarball unpacks to a versioned directory of its own.
    if [ ! -x "$dir/zig" ]; then
        mkdir -p "$PREFIX"
        local tmp="$PREFIX/zig-dl.tar.xz"
        say "    downloading $url"
        curl -fsSL -o "$tmp" "$url"
        rm -rf "$dir" "$PREFIX/zig-x86_64-linux-$ZIG_VERSION"
        tar -xJf "$tmp" -C "$PREFIX"
        mv "$PREFIX/zig-x86_64-linux-$ZIG_VERSION" "$dir"
        rm -f "$tmp"
    fi
    mkdir -p "$PREFIX/bin"
    ln -sf "$dir/zig" "$PREFIX/bin/zig"
    say "    installed $("$dir/zig" version) at $dir/zig"
}

# ── Idris2 ───────────────────────────────────────────────────────────────────
install_idris2() {
    say ""
    say "==> idris2 $IDRIS2_VERSION (bootstrapped with Chez Scheme)"
    local dir="$PREFIX/idris2-$IDRIS2_VERSION"
    if [ "$IDRIS2_BROKEN" -eq 1 ] && [ -d "$dir" ]; then
        say "    removing the broken install at $dir before rebuilding it"
        rm -rf "$dir"
    fi
    if [ ! -x "$dir/bin/idris2" ]; then
        if ! have chezscheme && ! have scheme && ! have chez; then
            say "    installing chezscheme (needs sudo)"
            sudo apt-get update -qq
            sudo apt-get install -y -qq chezscheme libgmp-dev build-essential
        fi
        local scheme
        scheme="$(command -v chezscheme || command -v scheme || command -v chez)"

        mkdir -p "$PREFIX/src"
        local src="$PREFIX/src/Idris2-$IDRIS2_VERSION"
        if [ ! -d "$src" ]; then
            say "    downloading Idris2 $IDRIS2_VERSION source"
            curl -fsSL -o "$PREFIX/src/idris2.tar.gz" \
                "https://github.com/idris-lang/Idris2/archive/refs/tags/v$IDRIS2_VERSION.tar.gz"
            tar -xzf "$PREFIX/src/idris2.tar.gz" -C "$PREFIX/src"
            rm -f "$PREFIX/src/idris2.tar.gz"
        fi

        # ONE prefix, given to BOTH steps. `make bootstrap` compiles the prefix
        # INTO the binary (src/IdrisPaths.idr, `yprefix`), and that compiled-in
        # prefix is where idris2 looks for everything at run time: its base
        # libraries and its backend support files alike.
        #
        # CORRECTED 2026-11-14. This used to pass PREFIX to `make install` only. The
        # bootstrap then compiled in the default, ~/.idris2, so the libraries (which
        # `make install` installs by running idris2 --install) followed the binary
        # to ~/.idris2, while the support files (installed by plain `make`) went to
        # $dir. The comment here read that split as a property of Idris2 and kept
        # it. The result type-checked and could not build a program, and the seam
        # gate needs to build one. With one prefix, everything is under $dir, and
        # ~/.idris2 is not used at all.
        #
        # `make clean` first: a source tree bootstrapped against another prefix
        # carries build products that the new prefix must not inherit.
        say "    bootstrapping (this takes minutes; -j1 because stage 2 is memory-hungry)"
        ( cd "$src" && make clean >/dev/null 2>&1 || true )
        ( cd "$src" && SCHEME="$scheme" make bootstrap -j1 PREFIX="$dir" )
        ( cd "$src" && make install PREFIX="$dir" )
    fi
    mkdir -p "$PREFIX/bin"
    ln -sf "$dir/bin/idris2" "$PREFIX/bin/idris2"
    say "    installed $("$PREFIX/bin/idris2" --version | head -1) at $dir/bin/idris2"
}

[ "$WANT_JUST" -eq 0 ] || [ "$JUST_HAVE" = "$JUST_VERSION" ] || install_just
[ "$WANT_ZIG" -eq 0 ]  || [ "$ZIG_HAVE" = "$ZIG_VERSION" ]   || install_zig
# A broken install at the right version is rebuilt too: the version is not the test.
if [ "$WANT_IDRIS2" -eq 1 ] && { [ "$IDRIS2_HAVE" != "$IDRIS2_VERSION" ] || [ "$IDRIS2_BROKEN" -eq 1 ]; }; then
    install_idris2
fi

# ── Verify, rather than announce ─────────────────────────────────────────────
# The point of a toolchain script is that the tools WORK, not that they exist. A
# compiler that starts and cannot find its own base library is the failure this
# catches, and it is exactly the failure that a version string does not reveal.
say ""
say ""
# The prefix is not on PATH yet — telling you to put it there is the next thing
# this script does — so the verification below MUST reach for the tools it just
# installed, not for whatever a shell happens to have. Without this line the
# check reported "just: command not found" while exiting 0, which is the worst
# shape a verification step can take.
export PATH="$PREFIX/bin:$PATH"
say "==> verifying the installed toolchain"
if [ "$WANT_IDRIS2" -eq 1 ]; then
    verify_rc=0
    verify_idris2 || verify_rc=$?
    if [ "$verify_rc" -eq 0 ]; then
        say "    idris2: type-checks a module importing Data.Nat, and builds and runs a program"
    else
        say "    FAIL: the idris2 just installed does not verify."
        explain_idris2_failure "$verify_rc"
        exit 1
    fi
fi
# Each requested tool must RUN at its pinned version, not merely exist on PATH.
if [ "$WANT_ZIG" -eq 1 ]; then
    [ "$(zig_version)" = "$ZIG_VERSION" ] || { say "    FAIL: zig does not run at $ZIG_VERSION"; exit 1; }
    say "    zig:    $(zig version) at $(command -v zig)"
fi
if [ "$WANT_JUST" -eq 1 ]; then
    [ "$(just_version)" = "$JUST_VERSION" ] || { say "    FAIL: just does not run at $JUST_VERSION"; exit 1; }
    say "    just:   $(just --version) at $(command -v just)"
fi

say "==================================================================="
say "Add the toolchain to this shell:"
say "    export PATH=\"$PREFIX/bin:\$PATH\""
say ""
say "Then, from the repository root:"
say "    just proof-check-idris2     # the ABI model and proofs must compile"
say "    just seam-check             # model -> header -> kernel -> both null hosts"
say "==================================================================="
