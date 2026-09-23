#! /bin/bash

build_date=$( date -u '+%Y_%m_%d-%H:%M %Z' )
tag=$( git -C $(dirname ${BASH_SOURCE[0]} ) log --pretty='%h' -n 1 )

sed -i -e "s/YYYY_MM_DD-HH:MM TZ @ HASH/$build_date @ $tag/" "$@"

