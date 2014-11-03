#!/bin/bash
set -x
BDIR=../..
PDIR=$BDIR/variant-package-arch64
mkdir -p $PDIR
cp $BDIR/tup.template.config $PDIR/tup.config
sed -i \
	-e "s/CONFIG_DEBUG.*$/CONFIG_DEBUG=false/" \
	$PDIR/tup.config
tup upd $PDIR
makepkg --repackage --noextract -f
