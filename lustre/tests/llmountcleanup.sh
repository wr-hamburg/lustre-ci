#!/bin/sh

SRCDIR="`dirname $0`"
. $SRCDIR/common.sh

umount /mnt/lustre || fail "cannot unmount"

killall acceptor
rmmod llite
rmmod mdc

$OBDCTL <<EOF
name2dev OSCDEV
cleanup
detach
name2dev LDLMDEV
cleanup
detach
name2dev RPCDEV
cleanup
detach
name2dev OSTDEV
cleanup
detach
name2dev FILTERDEV
cleanup
detach
name2dev MDSDEV
cleanup
detach
quit
EOF

rmmod obdecho
rmmod mds
rmmod osc
rmmod ost
rmmod obdfilter
rmmod obdext2
rmmod ldlm
rmmod ptlrpc
rmmod obdclass

$PTLCTL <<EOF
setup tcp
disconnect
del_uuid self
del_uuid mds
del_uuid ost
del_uuid ldlm
quit
EOF

rmmod kqswnal
rmmod ksocknal
rmmod portals

losetup -d ${LOOP}0
losetup -d ${LOOP}1
losetup -d ${LOOP}2
