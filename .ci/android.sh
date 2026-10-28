#!/bin/bash -ex

export NDK_CCACHE=$(command -v ccache)

# Keep secret checks and decoding out of the script's xtrace output.
set +x
has_keystore="${ANDROID_KEYSTORE_B64:-}"
has_alias="${ANDROID_KEY_ALIAS:-}"
has_password="${ANDROID_KEYSTORE_PASS:-}"
release_signing_required="${ANDROID_REQUIRE_RELEASE_SIGNING:-false}"

if [[ "$release_signing_required" == "true" && ( -z "$has_keystore" || -z "$has_alias" || -z "$has_password" ) ]]; then
    echo "A release-branch Android build requires ANDROID_KEYSTORE_B64, ANDROID_KEY_ALIAS, and ANDROID_KEYSTORE_PASS." >&2
    exit 1
fi

if [[ -n "$has_keystore" || -n "$has_alias" || -n "$has_password" ]]; then
    if [[ -z "$has_keystore" || -z "$has_alias" || -z "$has_password" ]]; then
        echo "Android signing secrets must be configured together." >&2
        exit 1
    fi
    export ANDROID_KEYSTORE_FILE="${GITHUB_WORKSPACE:-$(pwd)}/.android-release.keystore"
    trap 'rm -f "$ANDROID_KEYSTORE_FILE"' EXIT
    umask 077
    base64 --decode <<< "${ANDROID_KEYSTORE_B64}" > "${ANDROID_KEYSTORE_FILE}"
    unset ANDROID_KEYSTORE_B64
fi
unset has_keystore has_alias has_password
set -x

cd src/android
chmod +x ./gradlew

if [[ "${ANDROID_APK_ONLY:-false}" == "true" ]]; then
    if [[ "$TARGET" != "vanilla" ]]; then
        echo "ANDROID_APK_ONLY only supports TARGET=vanilla." >&2
        exit 1
    fi
    ./gradlew assembleVanillaRelease
elif [[ "$TARGET" == "googleplay" ]]; then
    ./gradlew assembleGooglePlayRelease
    ./gradlew bundleGooglePlayRelease
else
    ./gradlew assembleVanillaRelease
    ./gradlew bundleVanillaRelease
fi

if [[ -n "${ANDROID_KEYSTORE_FILE:-}" && -n "${GITHUB_OUTPUT:-}" ]]; then
    # Verify the APK signer against the configured keystore without tracing secrets.
    set +x
    certificate_sha256=$(keytool -exportcert \
        -keystore "$ANDROID_KEYSTORE_FILE" \
        -alias "$ANDROID_KEY_ALIAS" \
        -storepass:env ANDROID_KEYSTORE_PASS | sha256sum | awk '{print toupper($1)}')
    echo "certificate_sha256=$certificate_sha256" >> "$GITHUB_OUTPUT"
    unset certificate_sha256
    set -x
fi

ccache -s -v
