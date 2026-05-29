#!/usr/bin/env bash
#
# macOS code-sign + Apple-notarize the dylib inside the CPack tarball.
#
# Runs after ci/circleci-build-macos-universal.sh and BEFORE
# ci/make-importable-tarball.sh so the importable tarball ships with a
# signed dylib. Modifies build/<name>.tar.gz in place.
#
# Required env (CircleCI project env vars):
#   MACOS_CERT_P12_B64    base64 of the Developer ID .p12 export
#                         (`base64 -i Certificates.p12 | pbcopy`)
#   MACOS_CERT_PASSWORD   .p12 password
#   APPLE_ID              Apple ID email
#   APPLE_TEAM_ID         10-char team ID
#   APPLE_APP_PASSWORD    app-specific password (xxxx-xxxx-xxxx-xxxx)
#
# If MACOS_CERT_P12_B64 is empty the script is a no-op, so local builds and
# forks without secrets work without modification.

set -euo pipefail

if [[ -z "${MACOS_CERT_P12_B64:-}" ]]; then
  echo "MACOS_CERT_P12_B64 not set; skipping macOS sign+notarize."
  exit 0
fi

# Trace every command for the first run so we can see exactly which line
# fails in the CircleCI log. Secrets are still masked by CircleCI's UI.
set -x

# Sanity-print env var fingerprints (length only, never the values) so we
# can tell that all five made it through. Bash forbids `${#VAR:-default}`
# (length + default in one expr); use a temp var instead.
_pwd="${MACOS_CERT_PASSWORD:-}"
_aid="${APPLE_ID:-}"
_tid="${APPLE_TEAM_ID:-}"
_apw="${APPLE_APP_PASSWORD:-}"
echo "Cert b64 length: ${#MACOS_CERT_P12_B64}"
echo "Cert pwd length: ${#_pwd}"
echo "Apple ID length: ${#_aid}"
echo "Team ID  length: ${#_tid}"
echo "App pwd  length: ${#_apw}"

: "${MACOS_CERT_PASSWORD:?need MACOS_CERT_PASSWORD}"
: "${APPLE_ID:?need APPLE_ID}"
: "${APPLE_TEAM_ID:?need APPLE_TEAM_ID}"
: "${APPLE_APP_PASSWORD:?need APPLE_APP_PASSWORD}"

# ── Import the cert into a temp keychain ────────────────────────────────
KEYCHAIN=$(mktemp -d)/build.keychain-db
KEYCHAIN_PWD=$(uuidgen)

security create-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN"

P12=$(mktemp)
# `echo` adds a trailing newline that some base64 impls treat as bad
# input. `printf '%s'` doesn't. We also strip any whitespace/CR the env
# var might have picked up via copy/paste from `pbcopy`.
printf '%s' "$MACOS_CERT_P12_B64" \
  | tr -d ' \t\r\n' \
  | base64 -d > "$P12"
echo "Decoded p12 size: $(wc -c < "$P12") bytes"
file "$P12" || true   # should report 'PKCS#12 Certificate Bag' or similar
# -f pkcs12 makes the format explicit instead of relying on autodetect.
security import "$P12" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" \
  -f pkcs12 \
  -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: \
  -s -k "$KEYCHAIN_PWD" "$KEYCHAIN"
# Make the temp keychain user-visible so codesign can find the identity.
security list-keychains -d user -s "$KEYCHAIN" \
  $(security list-keychains -d user | sed s/\"//g)
rm -f "$P12"

trap 'security delete-keychain "$KEYCHAIN" 2>/dev/null || true' EXIT

# Install Apple's Developer ID intermediate certs into the temp keychain.
# Without them, the leaf cert's chain doesn't validate and
# `security find-identity -v` reports 0 valid identities even though the
# cert + private key are present. CircleCI's macOS image doesn't have
# these in any user-readable keychain by default.
echo "=== Installing Apple Developer ID intermediates ==="
for url in \
    https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer \
    https://www.apple.com/certificateauthority/DeveloperIDCA.cer \
    https://www.apple.com/appleca/AppleIncRootCertificate.cer ; do
  cer=$(mktemp).cer
  if curl -sSL -o "$cer" "$url"; then
    security import "$cer" -k "$KEYCHAIN" -T /usr/bin/codesign 2>&1 \
      | sed 's/^/  /' || true
  else
    echo "  (skipped, fetch failed: $url)"
  fi
  rm -f "$cer"
done

# Wait for Apple's TSA to be reachable. Tag-build of v0.1.0.0-alpha1
# died with `errSecInternalComponent` after the "unable to build chain
# to self-signed root" warning — almost always a transient TSA outage
# rather than a missing chain (`find-identity -v` reported the leaf
# valid). Probe Apple's timestamp endpoint before invoking codesign so
# we either confirm reachability or fail fast with a clear log line
# instead of mid-signing.
echo "=== TSA reachability check ==="
for tsa in http://timestamp.apple.com/ts01 http://timestamp.apple.com ; do
  if curl -sf --max-time 10 -o /dev/null "$tsa"; then
    echo "  $tsa: ok"
  else
    echo "  $tsa: unreachable (status $?)"
  fi
done

echo "=== All certs in the temp keychain ==="
security find-certificate -a -p "$KEYCHAIN" \
  | openssl x509 -noout -subject 2>/dev/null || true
echo "=== All identities (no -v, no policy) ==="
security find-identity "$KEYCHAIN" || true
echo "=== Valid identities (any policy) ==="
security find-identity -v "$KEYCHAIN" || true
echo "=== Valid code-signing identities ==="
security find-identity -v -p codesigning "$KEYCHAIN" || true

# Match the Developer ID Application cert. Tries the specific name first,
# then falls back to any code-signing identity if the cert happens to be
# named differently.
CODESIGN_ID=$(security find-identity -v -p codesigning "$KEYCHAIN" \
  | grep -m 1 "Developer ID Application" \
  | awk -F'"' '{print $2}' || true)
if [[ -z "$CODESIGN_ID" ]]; then
  # Try plain "Developer ID" in case Apple renamed it in the .p12 export.
  CODESIGN_ID=$(security find-identity -v -p codesigning "$KEYCHAIN" \
    | grep -m 1 "Developer ID" \
    | awk -F'"' '{print $2}' || true)
fi
if [[ -z "$CODESIGN_ID" ]]; then
  echo "ERROR: no Developer ID Application identity found."
  echo "Check that the cert exported to MACOS_CERT_P12_B64 is a"
  echo "  'Developer ID Application' cert (NOT 'Apple Development',"
  echo "  'Apple Distribution', 'Mac Installer', etc.)."
  echo "Re-create at https://developer.apple.com/account/resources/certificates/add"
  echo "  -> Software -> Developer ID Application."
  exit 1
fi
echo "Signing identity: $CODESIGN_ID"

# ── Extract the CPack tarball, sign every dylib, repack ─────────────────
cd build
TARBALL=$(ls -1t *.tar.gz 2>/dev/null \
  | grep -v -- '-import\.tar\.gz' \
  | head -1 || true)
if [[ -z "$TARBALL" ]]; then
  echo "ERROR: no CPack tarball in build/"
  exit 1
fi
echo "Signing dylibs inside: $TARBALL"

WORK=$(mktemp -d)
tar -C "$WORK" -xzf "$TARBALL"

DYLIB_COUNT=0
while IFS= read -r dylib; do
  echo ">> codesign $dylib"
  # --options runtime is required for Apple to notarize.
  # --timestamp pulls a secure timestamp from Apple's TSA so the signature
  # is verifiable forever (without it, sigs expire when the cert does).
  # Retry up to 3 times with backoff — the TSA call occasionally returns
  # errSecInternalComponent on transient network blips on CircleCI's
  # macOS executor, and re-running normally succeeds.
  attempt=0
  until codesign --deep --force --options runtime \
        --sign "$CODESIGN_ID" --timestamp \
        "$dylib"; do
    attempt=$((attempt + 1))
    if [[ "$attempt" -ge 3 ]]; then
      echo "ERROR: codesign failed 3× on $dylib"
      exit 1
    fi
    echo "codesign attempt $attempt failed, retrying in $((attempt * 10))s..."
    sleep $((attempt * 10))
  done
  codesign --verify --verbose=2 "$dylib"
  DYLIB_COUNT=$((DYLIB_COUNT + 1))
done < <(find "$WORK" -name '*.dylib')

if [[ "$DYLIB_COUNT" -eq 0 ]]; then
  echo "ERROR: no .dylib found in the tarball"
  exit 1
fi
echo "Signed $DYLIB_COUNT dylib(s)."

# Repack preserving CPack's single top-level wrapper directory.
TOPDIRS=$(ls -A "$WORK")
TOPCOUNT=$(echo "$TOPDIRS" | wc -l | tr -d ' ')
if [[ "$TOPCOUNT" == "1" ]]; then
  tar -C "$WORK" -czf "$TARBALL" "$TOPDIRS"
else
  tar -C "$WORK" -czf "$TARBALL" .
fi
rm -rf "$WORK"

# ── Notarize ────────────────────────────────────────────────────────────
WORK2=$(mktemp -d)
ZIP="$WORK2/notarize.zip"
tar -C "$WORK2" -xzf "$TARBALL"
DYLIB_PATH=$(find "$WORK2" -name '*.dylib' | head -1)

# Use the first dylib for notarization. OpenCPN plugins are one dylib;
# notarytool will record approval for that signed binary, which is what
# Gatekeeper checks at load time on the user's machine.
ditto -c -k --keepParent "$DYLIB_PATH" "$ZIP"

echo "Submitting notarization (max wait 30m)..."
# Strip whitespace from credentials in case the env var carries trailing
# spaces / a CR from copy-paste. Has bitten us once already on
# MACOS_CERT_P12_B64.
APPLE_ID_TRIM=$(printf '%s' "$APPLE_ID" | tr -d ' \t\r\n')
APPLE_TEAM_ID_TRIM=$(printf '%s' "$APPLE_TEAM_ID" | tr -d ' \t\r\n')
APPLE_APP_PASSWORD_TRIM=$(printf '%s' "$APPLE_APP_PASSWORD" | tr -d ' \t\r\n')

# Quick credential check before the actual submit. notarytool history is a
# read-only call against the same auth path, so if this returns 401 we know
# it's the credentials, not the zip we built.
echo "Pre-flighting credentials via 'notarytool history'..."
xcrun notarytool history \
  --apple-id "$APPLE_ID_TRIM" \
  --team-id "$APPLE_TEAM_ID_TRIM" \
  --password "$APPLE_APP_PASSWORD_TRIM" 2>&1 | head -10 || true

# Soft-fail the notarization step. Signing is what Gatekeeper actually
# checks; notarization removes the "developer cannot be verified" dialog
# on first install. If the credentials are bad we still want the rest of
# the pipeline (importable tarball, Cloudsmith upload) to ship the
# SIGNED dylib so the macOS Alpha lands alongside the other platforms.
# Fix the cred and the next push will pick it up.
set +e
xcrun notarytool submit "$ZIP" \
  --apple-id "$APPLE_ID_TRIM" \
  --team-id "$APPLE_TEAM_ID_TRIM" \
  --password "$APPLE_APP_PASSWORD_TRIM" \
  --wait \
  --timeout 30m
NOTARIZE_RC=$?
set -e
if [[ "$NOTARIZE_RC" -ne 0 ]]; then
  echo ""
  echo "::warning:: Notarization failed (exit $NOTARIZE_RC) but the dylib"
  echo "::warning:: is signed. Shipping signed-but-not-notarized tarball."
  echo "::warning:: Users will see a one-time Gatekeeper dialog; right-click"
  echo "::warning:: -> Open dismisses it permanently. Fix the credentials"
  echo "::warning:: at https://account.apple.com to flip the notary green."
fi

rm -rf "$WORK2"
echo "macOS sign + notarize: done."
