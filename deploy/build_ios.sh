#!/bin/bash
echo "Build script started ..."

set -o errexit -o nounset

# Hold on to current directory
PROJECT_DIR=$(pwd)

BUILD_DIR=$PROJECT_DIR/build-ios
mkdir -p $BUILD_DIR

echo "Project dir: ${PROJECT_DIR}"
echo "Build dir: ${BUILD_DIR}"

APP_NAME=ZloVPN
APP_FILENAME=$APP_NAME.app
APP_DOMAIN=com.zloserver.vpn
PLIST_NAME=$APP_NAME.plist


# Search Qt
if [ -z "${QT_VERSION+x}" ]; then
  QT_VERSION=6.5.3;
  QT_BIN_DIR=$HOME/Qt/$QT_VERSION/ios/bin
fi

echo "Using Qt in $QT_BIN_DIR"

# Checking env
$QT_BIN_DIR/qt-cmake --version
cmake --version
clang -v

# Generate XCodeProj
"$QT_BIN_DIR/qt-cmake" . \
    -B "$BUILD_DIR" \
    -G Xcode \
    -DQT_HOST_PATH="$QT_MACOS_ROOT_DIR" \
    -DCMAKE_AUTOMOC_COMPILER_PREDEFINES=OFF

KEYCHAIN=amnezia.build.ios.keychain
KEYCHAIN_FILE=$HOME/Library/Keychains/${KEYCHAIN}-db

# Setup keychain
if [ "${IOS_SIGNING_CERT_BASE64+x}" ]; then
  echo "Import certificate"

  SIGNING_CERT_P12=$BUILD_DIR/signing-cert.p12

  echo $IOS_SIGNING_CERT_BASE64 | base64 --decode > $SIGNING_CERT_P12

  shasum -a 256 $SIGNING_CERT_P12

  KEYCHAIN_PASS=$IOS_SIGNING_CERT_PASSWORD

  security create-keychain -p $KEYCHAIN_PASS $KEYCHAIN || true
  security default-keychain -s $KEYCHAIN
  security unlock-keychain -p $KEYCHAIN_PASS $KEYCHAIN

  security default-keychain
  security list-keychains

  security import $SIGNING_CERT_P12 -k $KEYCHAIN -P $IOS_SIGNING_CERT_PASSWORD -T /usr/bin/codesign

  security set-key-partition-list -S "apple-tool:,apple:,codesign:" -s -k $KEYCHAIN_PASS $KEYCHAIN
  security find-identity -p codesigning
  security set-keychain-settings $KEYCHAIN_FILE
  security set-keychain-settings -t 3600 $KEYCHAIN_FILE
  security unlock-keychain -p $KEYCHAIN_PASS $KEYCHAIN_FILE

  # Copy provisioning prifiles
  echo "Copy provisioning files"
  mkdir -p  "$HOME/Library/MobileDevice/Provisioning Profiles/"

  echo $IOS_APP_PROVISIONING_PROFILE | base64 --decode > ~/Library/MobileDevice/Provisioning\ Profiles/app.mobileprovision
  echo $IOS_NE_PROVISIONING_PROFILE | base64 --decode > ~/Library/MobileDevice/Provisioning\ Profiles/ne.mobileprovision

  shasum -a 256 ~/Library/MobileDevice/Provisioning\ Profiles/app.mobileprovision
  shasum -a 256 ~/Library/MobileDevice/Provisioning\ Profiles/ne.mobileprovision

  profile_uuid=`grep UUID -A1 -a ~/Library/MobileDevice/Provisioning\ Profiles/app.mobileprovision | grep -io "[-A-F0-9]\{36\}"`
  profile_ne_uuid=`grep UUID -A1 -a ~/Library/MobileDevice/Provisioning\ Profiles/ne.mobileprovision | grep -io "[-A-F0-9]\{36\}"`

  mv ~/Library/MobileDevice/Provisioning\ Profiles/app.mobileprovision ~/Library/MobileDevice/Provisioning\ Profiles/$profile_uuid.mobileprovision
  mv ~/Library/MobileDevice/Provisioning\ Profiles/ne.mobileprovision ~/Library/MobileDevice/Provisioning\ Profiles/$profile_ne_uuid.mobileprovision
else
  echo "Failed to import certificate, aborting..."
  exit 1
fi

# Build project
BUILD_CONFIG=RelWithDebInfo
ARCHIVE_PATH=$PROJECT_DIR/archive-ios.xcarchive
mkdir -p $ARCHIVE_PATH

xcodebuild archive \
"OTHER_CODE_SIGN_FLAGS=--keychain '$KEYCHAIN_FILE'" \
-configuration $BUILD_CONFIG \
-scheme ZloVPN \
-destination "generic/platform=iOS,name=Any iOS" \
-project $BUILD_DIR/ZloVPN.xcodeproj \
-archivePath $ARCHIVE_PATH

cp -R $BUILD_DIR/client/$BUILD_CONFIG-iphoneos/*.dSYM $ARCHIVE_PATH/dSYMs/
cp -R $BUILD_DIR/client/ios/networkextension/$BUILD_CONFIG-iphoneos/*.dSYM $ARCHIVE_PATH/dSYMs/

# Zip dSYMs

pushd $ARCHIVE_PATH/dSYMs/
zip -r $OLDPWD/ZloVPN.dSYM.zip *
popd

# Export to ipa
IOS_EXPORT=$PROJECT_DIR/ios-export
IPA_PATH=$PROJECT_DIR/ipa-ios
mkdir -p $IPA_PATH

xcodebuild -exportArchive \
-archivePath $ARCHIVE_PATH \
-exportPath $IPA_PATH \
-exportOptionsPlist $IOS_EXPORT/ExportOptions.plist

# restore keychain
security default-keychain -s login.keychain
