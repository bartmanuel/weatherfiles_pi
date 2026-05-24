#! /bin/bash

# This script refactors this plugin into a new plugin with a name of your choice.
# To rename the plugin to "catsarebest", run `bash make-new-plugin.sh catsarebest`.

newname="$1"
Newname="$(echo "${newname^}")"
NEWNAME="$(echo "${newname^^}")"

grep -rl weatherfiles . | grep -v .git | while read name; do
  sed -e "s+weatherfiles+$newname+g" -i "$name";
done

grep -rl Weatherfiles . | grep -v .git | while read name; do  
  sed -e "s+Weatherfiles+$Newname+g" -i "$name";
done 

grep -rl WEATHERFILES . | grep -v .git | while read name; do  
  sed -e "s+WEATHERFILES+$NEWNAME+g" -i "$name";
done 

find . -name "*weatherfiles*" | grep -v .git | while read name; do
  mv "$name" "$(echo "$name" | sed -e "s+weatherfiles+$newname+g")"
done

find . -name "*Weatherfiles*" | grep -v .git | while read name; do
  mv "$name" "$(echo "$name" | sed -e "s+Weatherfiles+$Newname+g")"
done
