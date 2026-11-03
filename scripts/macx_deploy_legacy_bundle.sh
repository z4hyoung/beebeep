#!/bin/bash
#
# This file is part of BeeBEEP.
#
# BeeBEEP is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version.
#
# BeeBEEP is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with BeeBEEP.  If not, see <http:#www.gnu.org/licenses/>.
#
# Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
#
# $Id: macx_deploy_legacy_bundle.sh 1609 2026-10-15 21:57:28Z mastroddi $
#
######################################################################

BEEBEEP_VERSION=5.8.6

echo "Making BeeBEEP Bundle version ${BEEBEEP_VERSION} LEGACY Qt 5.6.3"

SOURCE_DIR=".."
echo "Source folder:" $SOURCE_DIR

BUNDLE_APP="BeeBEEP.app"
BUNDLE_FOLDER="${BUNDLE_APP}"
echo "Bundle folder:" $BUNDLE_FOLDER

BUNDLE_DMG="BeeBEEP.dmg"
echo "Bundle:" $BUNDLE_DMG

MACDEPLOY_APP=../../Qt/5.6.3/clang_64/bin/macdeployqt
echo "Mac Deploy App:" $MACDEPLOY_APP

# delete previous bundle folder
printf "Delete previous bundle folder ... "
rm -rf $BUNDLE_FOLDER
echo "Done"

# delete previous bundle dmg
printf "Delete previous bundle ... "
rm -f BUNDLE_DMG
rm -f *.dmg
echo "Done"

#copy beebeep.app from release
printf "Copy beebeep.app ... "
cp -R $SOURCE_DIR/test/$BUNDLE_APP .
echo "Done"

#clean up
printf "Clean up folders and files ... "
rm -f $BUNDLE_FOLDER/Contents/Resources/beehosts.ini
rm -f $BUNDLE_FOLDER/Contents/Resources/beebeep.ini
rm -f $BUNDLE_FOLDER/Contents/Resources/beebeep.dat
rm -f $BUNDLE_FOLDER/Contents/Resources/beebeep.rc
rm -f $BUNDLE_FOLDER/Contents/Resources/qt.conf
echo "Done"

#create folders
printf "Create folders ... "
mkdir $BUNDLE_FOLDER/Contents/Frameworks
mkdir $BUNDLE_FOLDER/Contents/PlugIns
echo "Done"

#copy Info.plist
printf "Copy LEGACY Info.plist ... "
cp $SOURCE_DIR/misc/Info_legacy.plist $BUNDLE_FOLDER/Contents/Info.plist
echo "Done"

#copy beep.wav
printf "Copy beep.wav ... "
cp $SOURCE_DIR/misc/beep.wav $BUNDLE_FOLDER/Contents/Resources/.
echo "Done"

#copy beehosts.ini
printf "Copy beehosts_example.ini ... "
cp $SOURCE_DIR/misc/beehosts_example.ini $BUNDLE_FOLDER/Contents/Resources/
echo "Done"

#copy beebeep.rc
printf "Copy beebeep_example.rc ... "
cp $SOURCE_DIR/misc/beebeep_example.rc $BUNDLE_FOLDER/Contents/Resources/
echo "Done"

#copy locale
printf "Copy translations ... "
cp $SOURCE_DIR/locale/*.qm $BUNDLE_FOLDER/Contents/Resources/.
printf "and removing xx locale..."
rm $BUNDLE_FOLDER/Contents/Resources/beebeep_xx.qm
echo "Done"

#copy plugins
printf "Copy plugins ... "
cp  $SOURCE_DIR/test/*.dylib $BUNDLE_FOLDER/Contents/PlugIns/.
echo "Done"

#mac deploy frameworks
printf "MacOS X deploy ... "
$MACDEPLOY_APP $BUNDLE_FOLDER
echo "Done"

# Signed and notarized like the current bundles; whether Qt 5.6.3's
# frameworks pass the notary service is to be seen on the first run.
# ====================================================================
# CODE SIGNING PROCESS (bottom-up, hardened runtime, as in macx_deploy_arm64_bundle.sh)
# ====================================================================
IDENTITY_NAME="Developer ID Application: Marco Mastroddi (3F9FLBSUAJ)"
# By its SHA-1: the keychain may hold the same certificate twice, and
# codesign refuses a name that matches more than one.
IDENTITY=`security find-identity -v -p codesigning | grep -F "\"$IDENTITY_NAME\"" | head -1 | awk '{ print $2 }'`
if [ -z "$IDENTITY" ]; then
  echo "Certificate not found: $IDENTITY_NAME"
  exit 1
fi
FLAGS="--timestamp --options runtime --force"

printf "Cleaning extended attributes... "
xattr -cr "$BUNDLE_FOLDER"
chmod -R u+w "$BUNDLE_FOLDER"
echo "Done"

printf "Signing PlugIns... "
find "$BUNDLE_FOLDER/Contents/PlugIns" -type f \( -name "*.dylib" -o -name "*.so" \) -exec codesign $FLAGS -s "$IDENTITY" {} \;
echo "Done"

printf "Signing Frameworks... "
find "$BUNDLE_FOLDER/Contents/Frameworks" -type f -name "*.dylib" -exec codesign $FLAGS -s "$IDENTITY" {} \;
find "$BUNDLE_FOLDER/Contents/Frameworks" -type f ! -name "*.dylib" ! -name "*.plist" ! -name "*.h" -exec codesign $FLAGS -s "$IDENTITY" {} \;
echo "Done"

printf "Signing main executable... "
codesign $FLAGS -s "$IDENTITY" "$BUNDLE_FOLDER/Contents/MacOS/beebeep"
echo "Done"

printf "Signing overall BeeBEEP.app bundle... "
codesign $FLAGS -s "$IDENTITY" "$BUNDLE_FOLDER"
codesign --verify --deep --strict "$BUNDLE_FOLDER"
echo "Done"

# ====================================================================
# DISTRIBUTABLE PACKAGE CREATION & SIGNING (DMG)
# ====================================================================
rm -rf *.dmg
rm -rf macosx_dmg
mkdir macosx_dmg
ln -s /Applications macosx_dmg
cp -a $BUNDLE_FOLDER macosx_dmg/

printf "Building DMG volume... "
hdiutil create -volname "BeeBEEP ${BEEBEEP_VERSION}" -srcfolder macosx_dmg -ov -format UDZO beebeep-${BEEBEEP_VERSION}-qt563.dmg > /dev/null
echo "Done"

printf "Signing final DMG file... "
# DMGs are signed with a timestamp but without the hardened runtime
codesign --timestamp -s "$IDENTITY" beebeep-${BEEBEEP_VERSION}-qt563.dmg
echo "Done"

printf "Making also ZIP version... "
ditto -c -k --sequesterRsrc --keepParent $BUNDLE_FOLDER beebeep-${BEEBEEP_VERSION}-qt563-osx.zip
echo "Done"

# ====================================================================
# APPLE NOTARIZATION PROCESS
# ====================================================================
# macx_notarize_bundle.sh: the App Store Connect API key in
# ~/.config/beebeep (or the keychain profile), notarize, staple, and the
# final Gatekeeper assessment.
echo "=== Submitting to Apple Notary Service ==="
if bash ./macx_notarize_bundle.sh beebeep-${BEEBEEP_VERSION}-qt563.dmg; then
    echo "Success: DMG notarized, stapled and accepted by Gatekeeper."
else
    echo "Error: notarization failed (see above)."
    exit 1
fi

rm -rf macosx_dmg
echo "BeeBEEP LEGACY Qt 5.6.3 Deployment Process Completed."
