#!/bin/sh

pidfiledir=/var/run
DEV=cuaa0
CMD1='AT\r'
CMD2='ATZ\r'

case "$1" in
start)
	/usr/home/dds/montty/montty $DEV $CMD1 $CMD2 $CMD3 && echo -n ' montty'
	;;
stop)
	kill `cat $pidfiledir/montty.$DEV.pid`
	;;
restart)
	kill `cat $pidfiledir/montty.$DEV.pid`
	rm -f /var/spool/lock/LCK..cuaa0
	sleep 1
	/usr/home/dds/montty/montty $DEV $CMD1 $CMD2 $CMD3 && echo -n ' montty'
	;;
*)
	echo "Usage: `basename $0` {start|stop}" >&2
	;;
esac

exit 0
