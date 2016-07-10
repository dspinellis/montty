#!/bin/sh

pidfiledir=/var/run
DEV=ttyACM0
CMD1='AT\r'
CMD2='ATZ\r'

sanemode()
{
  stty -F /dev/$DEV cs8 115200 ignbrk -brkint -icrnl -imaxbel -opost \
    -onlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke \
    noflsh -ixon -crtscts
}

case "$1" in
start)
  sanemode
  /usr/local/sbin/montty $DEV $CMD1 $CMD2 $CMD3 && echo -n ' montty'
  ;;
stop)
  kill `cat $pidfiledir/montty.$DEV.pid`
  ;;
restart)
  kill `cat $pidfiledir/montty.$DEV.pid`
  rm -f /var/spool/lock/LCK..cuaa0
  sleep 1
  sanemode
  /usr/home/dds/montty/montty $DEV $CMD1 $CMD2 $CMD3 && echo -n ' montty'
  ;;
*)
  echo "Usage: `basename $0` {start|stop}" >&2
  ;;
esac

exit 0
