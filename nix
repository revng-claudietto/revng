#!/usr/bin/env bash

# Self-contained nix wrapper for the revng repo. On first run, downloads
# a pinned nix-portable, writes an isolated nix.conf configured with the
# rev.ng public and private HTTP binary caches, prompts for a GitLab
# token to authenticate to the private cache, and sanity-checks both
# endpoints. Subsequent runs just exec nix inside nix-portable.
#
# State (nix-portable binary, isolated store, config, netrc) lives either
# in a shared XDG cache directory (default: $XDG_CACHE_HOME/revng/nix,
# i.e. ~/.cache/revng/nix), so multiple checkouts of the repo share the
# same store, or in ./.nix next to this script, local to the checkout.
# On first run the script asks which one to use. Delete the picked
# directory to reset.
#
# Example:
#   ./nix build .#revng -j0     # substitute the closure, no local build

set -euo pipefail

# Logging helper: writes to stderr, so stdout stays clean for whatever
# nix-portable produces. Accepts either arguments or stdin (for multi-
# line heredocs).
log() {
    if [ "$#" -eq 0 ]; then
        cat >&2
    else
        printf '%s\n' "$*" >&2
    fi
}

# Pinned nix-portable release.
NIX_PORTABLE_VERSION="${NIX_PORTABLE_VERSION:-v012}"

# x86_64 / aarch64.
NIX_PORTABLE_ARCHITECTURE="$(uname -m)"

NIX_PORTABLE_URL="https://github.com/DavHau/nix-portable/releases/download/${NIX_PORTABLE_VERSION}/nix-portable-${NIX_PORTABLE_ARCHITECTURE}"

# Modern static nix used for the actual work; nix-portable is only a
# bootstrap. nix-portable bundles nix 2.20.6, which predates the
# nix-specific NIX_CACHE_HOME / NIX_STATE_HOME / NIX_CONFIG_HOME vars (added
# in nix 2.25). On 2.20.6 the only way to move nix's per-user caches off
# ~/.cache is XDG_CACHE_HOME, which would also redirect every other tool run
# inside "./nix develop". This static nix honors the nix-only vars, so all
# state stays under NIX_DIRECTORY with nothing leaking into the environment.
#
# It is pinned as a fixed cache.nixos.org store path so bootstrapping is a
# plain substitution (no flake evaluation under the slower, less robust
# 2.20.6). To bump the version, on a machine with a modern nix run:
#   nix build github:NixOS/nix/<tag>#nix-cli-static --print-out-paths
# and paste the resulting /nix/store/... path here.
MODERN_NIX_STORE_PATH="${MODERN_NIX_STORE_PATH:-/nix/store/h8fldrbcpyr5s9f90b313ybkr6rrysyi-nix-static-x86_64-unknown-linux-musl-2.26.2}"

# Candidate state directories.
SCRIPT_DIRECTORY="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
XDG_CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}"
CACHE_DIRECTORY="$XDG_CACHE_ROOT/revng/nix"
LOCAL_DIRECTORY="$SCRIPT_DIRECTORY/.nix"

# The extracted nix-portable binary lives at this path in each candidate.
# If we find it in either, that directory wins and there is no prompt.
CACHE_BINARY="$CACHE_DIRECTORY/nix-portable/nix"
LOCAL_BINARY="$LOCAL_DIRECTORY/nix-portable/nix"

if   [ -x "$CACHE_BINARY" ]; then
    NIX_DIRECTORY="$CACHE_DIRECTORY"
elif [ -x "$LOCAL_BINARY" ]; then
    NIX_DIRECTORY="$LOCAL_DIRECTORY"
else
    log <<EOF
No existing nix-portable install found. Where should it live?

  1) $CACHE_DIRECTORY
     Shared across all revng checkouts (recommended if you have more
     than one checkout — you download nix-portable and populate the
     store once).

  2) $LOCAL_DIRECTORY
     Scoped to this checkout only.

EOF
    read -r -p "Choose [1/2] (default 1): " ANSWER
    case "${ANSWER:-1}" in
        1|"") NIX_DIRECTORY="$CACHE_DIRECTORY" ;;
        2)    NIX_DIRECTORY="$LOCAL_DIRECTORY" ;;
        *)    log "Invalid choice, aborting."; exit 1 ;;
    esac
fi

NIX_PORTABLE_BINARY="$NIX_DIRECTORY/nix-portable/nix"
CONFIG_FILE="$NIX_DIRECTORY/nix.conf"
NETRC_FILE="$NIX_DIRECTORY/netrc"

# Written only after a fully-successful setup. If missing, we redo the
# token prompt, config generation, and sanity check (the nix-portable
# binary is cached so we don't re-download it).
SETUP_COMPLETE_MARKER="$NIX_DIRECTORY/.setup-complete"

# nix-portable stores its unpacked runtime and store under $NP_LOCATION.
export NP_LOCATION="$NIX_DIRECTORY"

# Reuse the host git if present, so nix-portable does not substitute a git
# into its store on first use (one less closure, faster bootstrap).
if command -v git >/dev/null 2>&1; then
    export NP_GIT="$(command -v git)"
fi

# rev.ng caches.
GATING_PROJECT_URL="https://rev.ng/gitlab/revng-private/binary-archives"

# GitLab's access-tokens form reads name / scopes[] / access_level from
# the query string, so the URL below prefills a Reporter-role,
# read_repository token named "nix-binary-cache" — the user just clicks
# Create.
TOKEN_URL="${GATING_PROJECT_URL}/-/settings/access_tokens?name=nix-binary-cache&scopes[]=read_repository&access_level=20"

PUBLIC_CACHE_URL="https://rev.ng/nix-binary-cache/public/"
PRIVATE_CACHE_URL="https://rev.ng/nix-binary-cache/private/"
REVNG_PUBLIC_KEY="revng-cache:Wqy0YTHRGuDijpuHK+3uhP54idwTYbjXfxVqnsfusGU="
NIXOS_PUBLIC_KEY="cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="

if [ ! -f "$SETUP_COMPLETE_MARKER" ]; then
    mkdir -p "$(dirname "$NIX_PORTABLE_BINARY")"

    # Skip the download if a previous run made it this far and then
    # failed at the sanity check.
    if [ ! -x "$NIX_PORTABLE_BINARY" ]; then
        log "Downloading pinned nix-portable ${NIX_PORTABLE_VERSION} (${NIX_PORTABLE_ARCHITECTURE}) into ${NIX_DIRECTORY}"
        curl -fsSL "$NIX_PORTABLE_URL" -o "$NIX_PORTABLE_BINARY.tmp"
        chmod +x "$NIX_PORTABLE_BINARY.tmp"
        mv "$NIX_PORTABLE_BINARY.tmp" "$NIX_PORTABLE_BINARY"
    fi

    # Token / netrc bootstrap.
    #
    # Nix's HTTP binary-cache client authenticates via netrc, i.e. HTTP
    # Basic. GitLab rejects Basic on its REST API but accepts a token as
    # the Basic password on git-over-HTTPS, which is what the private-
    # cache nginx auth_request forwards to. The token goes in the
    # password field; any non-empty login works.
    #
    # The token is OPTIONAL: skipping it configures the public cache
    # only, which is enough if the closures you build don't reference
    # any `fetchPrivateUrl`-produced paths (Windows SDKs, non-
    # redistributable tarballs, and so on).
    log <<EOF

The private cache is gated on read access to
  ${GATING_PROJECT_URL}

Create a project access token (form is prefilled: scope read_repository,
role Reporter) at:

  ${TOKEN_URL}

Leave the prompt empty to configure the public cache only — that is
enough unless you build something that pulls a private FOD (e.g. a
Windows SDK).

EOF
    read -r -s -p "Paste the token (input hidden, or ENTER to skip): " TOKEN
    log ""

    # nix.conf, plus optional netrc: two shapes depending on whether the
    # private cache is wired in.
    if [ -n "$TOKEN" ]; then
        ( umask 077
          cat > "$NETRC_FILE" <<EOF
machine rev.ng
  login nix-cache
  password $TOKEN
EOF
        )
        chmod 600 "$NETRC_FILE"
        cat > "$CONFIG_FILE" <<EOF
experimental-features = nix-command flakes
substituters = ${PUBLIC_CACHE_URL} ${PRIVATE_CACHE_URL} https://cache.nixos.org/
trusted-public-keys = ${REVNG_PUBLIC_KEY} ${NIXOS_PUBLIC_KEY}
netrc-file = ${NETRC_FILE}
EOF
    else
        rm -f "$NETRC_FILE"
        cat > "$CONFIG_FILE" <<EOF
experimental-features = nix-command flakes
substituters = ${PUBLIC_CACHE_URL} https://cache.nixos.org/
trusted-public-keys = ${REVNG_PUBLIC_KEY} ${NIXOS_PUBLIC_KEY}
EOF
    fi

    # Sanity check. Always verify the public cache. Only verify the
    # private cache if a token was supplied. Failures come with clear
    # HTTP codes because we do this in curl instead of through nix.
    log ""
    log "Sanity check: nix-cache-info on the configured cache(s)."
    PUBLIC_CODE=$(curl -sS -o /dev/null -w "%{http_code}" -m 15 "${PUBLIC_CACHE_URL}nix-cache-info" || echo error)
    log "    public   -> $PUBLIC_CODE"
    CHECK_FAILED=0
    [ "$PUBLIC_CODE" != 200 ] && CHECK_FAILED=1

    if [ -n "$TOKEN" ]; then
        PRIVATE_CODE=$(curl -sS -o /dev/null -w "%{http_code}" -m 15 -u "nix-cache:$TOKEN" "${PRIVATE_CACHE_URL}nix-cache-info" || echo error)
        log "    private  -> $PRIVATE_CODE"
        [ "$PRIVATE_CODE" != 200 ] && CHECK_FAILED=1
    else
        log "    private  -> skipped (no token)"
    fi

    if [ "$CHECK_FAILED" != 0 ]; then
        log "Sanity check failed. Rerun this script to retry."
        if [ -n "${PRIVATE_CODE:-}" ] && { [ "$PRIVATE_CODE" = 401 ] || [ "$PRIVATE_CODE" = 403 ]; }; then
            log "Private returned $PRIVATE_CODE: check that the token has read_repository"
            log "access on ${GATING_PROJECT_URL} (Reporter role or higher)."
        fi

        # So the next run re-prompts.
        rm -f "$NETRC_FILE" "$CONFIG_FILE"
        exit 1
    fi

    touch "$SETUP_COMPLETE_MARKER"
    log "Setup complete."
    log ""
fi

# Bootstrap the modern static nix, once. nix-portable's 2.20.6 realises the
# pinned store path straight from the binary caches (a plain substitution,
# no flake evaluation), then we copy the static binary out so it runs
# standalone from NIX_DIRECTORY. The store path lives inside nix-portable's
# relocated store at $NP_LOCATION/.nix-portable/nix/store.
MODERN_NIX="$NIX_DIRECTORY/nix-modern/bin/nix"
if [ ! -x "$MODERN_NIX" ]; then
    log "Bootstrapping modern static nix ($(basename "$MODERN_NIX_STORE_PATH"))"

    # Give the bootstrap nix its own cache dir: a stale entry in the shared
    # host ~/.cache/nix can make 2.20.6 spin forever on an sqlite busy loop.
    # This XDG override is safe: the bootstrap is internal and never execs a
    # shell, so it cannot leak into "./nix develop".
    XDG_CACHE_HOME="$NIX_DIRECTORY/.bootstrap-cache" \
    NIX_USER_CONF_FILES="$CONFIG_FILE" \
        "$NIX_PORTABLE_BINARY" build "$MODERN_NIX_STORE_PATH" --no-link >/dev/null

    mkdir -p "$(dirname "$MODERN_NIX")"
    cp "$NP_LOCATION/.nix-portable${MODERN_NIX_STORE_PATH}/bin/nix" "$MODERN_NIX"
    chmod +x "$MODERN_NIX"
    log "Modern nix ready: $("$MODERN_NIX" --version)"
fi

# Run the modern nix with every per-user path pinned inside NIX_DIRECTORY.
# NIX_*_HOME are honored only when use-xdg-base-directories is on (set via
# NIX_CONFIG below). Unlike XDG_CACHE_HOME these are read by nix alone, so
# nothing here leaks into a "./nix develop" shell or the tools run in it.
export NIX_CACHE_HOME="$NIX_DIRECTORY/nix/cache"
export NIX_STATE_HOME="$NIX_DIRECTORY/nix/state"
export NIX_CONFIG_HOME="$NIX_DIRECTORY/nix/config"
mkdir -p "$NIX_CACHE_HOME" "$NIX_STATE_HOME" "$NIX_CONFIG_HOME"

# NIX_USER_CONF_FILES fully replaces ~/.config/nix/nix.conf: substituters,
# trusted keys, netrc and experimental-features all come from our generated
# nix.conf. use-xdg-base-directories switches on the NIX_*_HOME lookup, and
# store keeps the store itself inside NIX_DIRECTORY too.
export NIX_USER_CONF_FILES="$CONFIG_FILE"
export NIX_CONFIG="use-xdg-base-directories = true
store = $NIX_DIRECTORY/store"

# The static nix has no built-in CA bundle; point it at the host's, falling
# back to the one nix-portable ships.
if [ -f /etc/ssl/certs/ca-certificates.crt ]; then
    export NIX_SSL_CERT_FILE="/etc/ssl/certs/ca-certificates.crt"
elif [ -f "$NP_LOCATION/.nix-portable/ca-bundle.crt" ]; then
    export NIX_SSL_CERT_FILE="$NP_LOCATION/.nix-portable/ca-bundle.crt"
fi

exec "$MODERN_NIX" "$@"
