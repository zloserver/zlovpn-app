#!/bin/bash
echo "Build script started ..."

set -o errexit -o nounset

while getopts n flag
do
    case "${flag}" in
        n) NOTARIZE_APP=1;;
    esac
done

# Hold on to current directory
PROJECT_DIR=$(pwd)
DEPLOY_DIR=$PROJECT_DIR/deploy

mkdir -p $DEPLOY_DIR/build
BUILD_DIR=$DEPLOY_DIR/build

echo "Project dir: ${PROJECT_DIR}" 
echo "Build dir: ${BUILD_DIR}"

APP_NAME=ZloVPN
APP_FILENAME=$APP_NAME.app
APP_DOMAIN=com.zloserver.vpn
PLIST_NAME=$APP_NAME.plist

BUILD_CONFIG=Release
OUT_APP_DIR=$BUILD_DIR/client/$BUILD_CONFIG
BUNDLE_DIR=$OUT_APP_DIR/$APP_FILENAME

PREBUILT_DEPLOY_DATA_DIR=$PROJECT_DIR/deploy/data/deploy-prebuilt/macos
DEPLOY_DATA_DIR=$PROJECT_DIR/deploy/data/macos

DMG_FILENAME=$PROJECT_DIR/${APP_NAME}.dmg

# Search Qt
if [ -z "${QT_VERSION+x}" ]; then
QT_VERSION=6.5.3;
QT_BIN_DIR=$HOME/Qt/$QT_VERSION/macos/bin
fi

echo "Using Qt in $QT_BIN_DIR"


# Checking env
$QT_BIN_DIR/qt-cmake --version
cmake --version
clang -v

# Prepare signing certs
KEYCHAIN=zlovpn.build.macos.keychain
KEYCHAIN_FILE=$HOME/Library/Keychains/${KEYCHAIN}-db

if [ "${MACOS_SIGNING_CERT_BASE64+x}" ]; then
  echo "Import certificate"

  SIGNING_CERT_P12=$BUILD_DIR/signing-cert.p12

  echo $MACOS_SIGNING_CERT_BASE64 | base64 --decode > $SIGNING_CERT_P12

  KEYCHAIN_PASS=$MACOS_SIGNING_CERT_PASSWORD

  security create-keychain -p $KEYCHAIN_PASS $KEYCHAIN || true
  security default-keychain -s $KEYCHAIN
  security unlock-keychain -p $KEYCHAIN_PASS $KEYCHAIN

  security default-keychain
  security list-keychains

  security import $SIGNING_CERT_P12 -k $KEYCHAIN -P $MACOS_SIGNING_CERT_PASSWORD -T /usr/bin/codesign

  security set-key-partition-list -S "apple-tool:,apple:,codesign:" -s -k $KEYCHAIN_PASS $KEYCHAIN
  security find-identity -p codesigning
  security set-keychain-settings $KEYCHAIN_FILE
  security set-keychain-settings -t 3600 $KEYCHAIN_FILE
  security unlock-keychain -p $KEYCHAIN_PASS $KEYCHAIN_FILE
fi

# Build App
echo "Building App..."
cd $BUILD_DIR

MACOS_SDK_PATH=$(xcrun --sdk macosx --show-sdk-path)
"$QT_BIN_DIR/qt-cmake" -S "$PROJECT_DIR" -B "$BUILD_DIR" -GXcode -DCMAKE_OSX_SYSROOT="$MACOS_SDK_PATH"
cmake --build . --config ${BUILD_CONFIG}

# Build and run tests here

echo "____________________________________"
echo "............Deploy.................."
echo "____________________________________"

# Package
echo "Packaging ..."

TMP_DIR=tmp-mac-deploy
rm -rf $TMP_DIR $DMG_FILENAME
mkdir -p $TMP_DIR
cp -R $BUNDLE_DIR $TMP_DIR/

create-dmg \
--volname "ZloVPN" \
--volicon $DEPLOY_DATA_DIR/dmg_icon.icns \
--background $DEPLOY_DATA_DIR/dmg_background.png \
--window-size 660 400 \
--icon "ZloVPN.app" 180 170 \
--app-drop-link 480 170 \
--icon-size 96 \
$DMG_FILENAME $TMP_DIR/

rm -rf $TMP_DIR

# Sign
if [ "${MACOS_SIGNING_CERT_BASE64+x}" ]; then
  echo "Signing ..."

  codesign -s $CODESIGN_ID --timestamp -i $APP_DOMAIN $DMG_FILENAME
  codesign --deep --force --verbose=2 --strict $DMG_FILENAME

  # todo: reenable this later
  # echo "Notarizing..."
  # xcrun notarytool submit $DMG_FILENAME --apiIssuer $APPSTORE_API_KEY_ISSUER --apiKey $APPSTORE_API_KEY_NAME --wait
  # xcrun stapler staple $DMG_FILENAME
fi

echo "Finished, artifact is $DMG_FILENAME"

# restore keychain
security default-keychain -s login.keychain
