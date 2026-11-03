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
# $Id: macx_notarize_bundle.sh 1609 2026-10-15 21:57:28Z mastroddi $
#
######################################################################

# Notarizes a signed disk image with Apple's notary service, staples the
# ticket to it and asks Gatekeeper to assess it.
#
# Usage: ./macx_notarize_bundle.sh [file.dmg]   (default: the first *.dmg here)
#
# Authentication: an App Store Connect API key (Users and Access ->
# Integrations -> App Store Connect API, access "Developer"). Its IDs are read
# from ~/.config/beebeep/notary-api.conf:
#
#   KEY_ID=<key id>
#   ISSUER_ID=<issuer id>
#
# and the key itself from ~/.config/beebeep/AuthKey_<key id>.p8 (chmod 600).
# Without that file, the notarytool keychain profile BEEBEEP_DEV_PASSWORD
# (xcrun notarytool store-credentials) is used instead.
# (altool, which this script used before, was retired by Apple in 2023.)

set -e

NOTARY_CONF="$HOME/.config/beebeep/notary-api.conf"
NOTARY_PROFILE="BEEBEEP_DEV_PASSWORD"

dmg_file="$1"
if [ -z "$dmg_file" ]; then
  dmg_file=`ls ./*.dmg 2> /dev/null | head -1`
fi
if [ -z "$dmg_file" ] || [ ! -f "$dmg_file" ]; then
  echo "DMG file not found."
  exit 1
fi

if [ -f "$NOTARY_CONF" ]; then
  key_id=`sed -n 's/^KEY_ID=//p' "$NOTARY_CONF" | head -1 | tr -d '[:space:]"'`
  issuer_id=`sed -n 's/^ISSUER_ID=//p' "$NOTARY_CONF" | head -1 | tr -d '[:space:]"'`
  key_file="$HOME/.config/beebeep/AuthKey_${key_id}.p8"
  if [ -z "$key_id" ] || [ -z "$issuer_id" ] || [ ! -f "$key_file" ]; then
    echo "Incomplete API key: $NOTARY_CONF needs KEY_ID and ISSUER_ID, and $key_file must exist."
    exit 1
  fi
  notary_auth=( --key "$key_file" --key-id "$key_id" --issuer "$issuer_id" )
  echo "Notarizing $dmg_file with the API key $key_id ..."
else
  notary_auth=( --keychain-profile "$NOTARY_PROFILE" )
  echo "Notarizing $dmg_file with the keychain profile $NOTARY_PROFILE ..."
fi

response_tmp_file="beebeep_notarization_response.plist"
rm -f "$response_tmp_file"
if ! xcrun notarytool submit "$dmg_file" "${notary_auth[@]}" --wait --output-format plist > "$response_tmp_file"; then
  echo "notarytool failed."
  rm -f "$response_tmp_file"
  exit 1
fi
status=`/usr/libexec/PlistBuddy -c 'Print :status' "$response_tmp_file"`
uuid=`/usr/libexec/PlistBuddy -c 'Print :id' "$response_tmp_file"`
rm -f "$response_tmp_file"
echo "Submission $uuid: $status"

if [ "$status" != "Accepted" ]; then
  xcrun notarytool log "$uuid" "${notary_auth[@]}" || true
  echo "Notarization rejected (log above)."
  exit 1
fi

xcrun stapler staple "$dmg_file"
echo "Stapler done."

if spctl --assess --type open --context context:primary-signature --verbose "$dmg_file"; then
  echo "Gatekeeper accepts $dmg_file."
else
  echo "Warning: Gatekeeper does not accept $dmg_file."
  exit 1
fi

echo "Done."
