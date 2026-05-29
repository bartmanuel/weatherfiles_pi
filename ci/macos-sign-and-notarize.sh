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
  codesign --deep --force --options runtime \
    --sign "$CODESIGN_ID" --timestamp \
    "$dylib"
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
xcrun notarytool submit "$ZIP" \
  --apple-id "$APPLE_ID" \
  --team-id "$APPLE_TEAM_ID" \
  --password "$APPLE_APP_PASSWORD" \
  --wait \
  --timeout 30m

rm -rf "$WORK2"
echo "macOS sign + notarize: done."
