#!/bin/sh
# Build a Navit map (binfile) from an OpenStreetMap extract. Runs inside the dev container:
#
#   make map NAME=italy REGION=europe/italy                      whole country
#   make map NAME=milano REGION=europe/italy/nord-ovest BBOX=8.9,45.3,9.4,45.6
#
#   REGION  path of an extract on download.geofabrik.de (without -latest.osm.pbf)
#   BBOX    optional crop: left,bottom,right,top in degrees
#   FILTER  roads (default): roads, ferries, turn restrictions, places, water, borders.
#           Everything else (buildings, POIs, land use) is dropped: the unit draws a smaller
#           map much faster. addresses: the same plus house numbers (for the address search).
#           full: keep everything.
#
# Output: apps/navit/maps/NAME.bin. The downloaded extract is kept in apps/navit/maps/src/ and
# reused until Geofabrik publishes a newer one. OpenStreetMap data: ODbL, (c) OpenStreetMap contributors.
set -eu
NAME=$1 REGION=$2 BBOX=${3:-} FILTER=${FILTER:-roads}
MAPS=$(cd "$(dirname "$0")" && pwd)/maps
SRC=$MAPS/src/$(echo "$REGION" | tr / _).osm.pbf
URL=https://download.geofabrik.de/$REGION-latest.osm.pbf
mkdir -p "$MAPS/src"

# 1. download (again only if the server has a newer file)
echo ">> $URL"
# into .part first: an interrupted download must not destroy the kept extract
rm -f "$SRC.part"
if [ -f "$SRC" ]; then
  curl -fL --progress-bar -z "$SRC" -o "$SRC.part" "$URL"
else
  curl -fL --progress-bar -o "$SRC.part" "$URL"
fi
[ ! -s "$SRC.part" ] || mv "$SRC.part" "$SRC"
rm -f "$SRC.part"
# check the published MD5; Geofabrik sometimes publishes none: then read the whole file instead
if MD5=$(curl -fsSL "$URL.md5"); then
  echo "$MD5" | awk -v f="$SRC" '{print $1 "  " f}' | md5sum -c --quiet
else
  echo ">> no MD5 published, checking the whole file"
  osmium fileinfo -e "$SRC" >/dev/null
fi

TMP=$(mktemp -d "$MAPS/.build-XXXXXX")
trap 'rm -rf "$TMP"' EXIT
IN=$SRC

# 2. crop
if [ -n "$BBOX" ]; then
  echo ">> crop to $BBOX"
  # complete borders too: maptool files every town under the country whose border encloses it,
  # and Navit's address search only finds towns that have a country
  osmium extract --bbox "$BBOX" --strategy smart -S types=multipolygon,boundary \
    -o "$TMP/crop.osm.pbf" "$IN"
  IN=$TMP/crop.osm.pbf
fi

# 3. keep only what a car map needs (referenced nodes and members come along)
if [ "$FILTER" = roads ] || [ "$FILTER" = addresses ]; then
  echo ">> filter: roads, places, water, borders$([ "$FILTER" = addresses ] && echo ", house numbers")"
  # addresses: house numbers on points and buildings, and interpolation lines between them
  ADDRESSES=; [ "$FILTER" = addresses ] && ADDRESSES="nwr/addr:housenumber w/addr:interpolation"
  # shellcheck disable=SC2086  # ADDRESSES is a list of filter expressions
  osmium tags-filter -o "$TMP/filtered.osm.pbf" "$IN" \
    w/highway w/route=ferry r/type=restriction \
    nwr/place \
    nwr/natural=water,coastline,bay nwr/water nwr/waterway=river,riverbank,canal \
    r/boundary=administrative $ADDRESSES
  IN=$TMP/filtered.osm.pbf
fi

# 4. convert (maptool writes its temporary files into the current directory)
echo ">> maptool -> $MAPS/$NAME.bin"
cd "$TMP"
maptool --protobuf -i "$IN" "$TMP/$NAME.bin"
mv "$TMP/$NAME.bin" "$MAPS/$NAME.bin"
ls -l "$MAPS/$NAME.bin"
