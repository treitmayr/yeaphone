#!/bin/bash

VID=6993
PIDS="b001 b700"

kvers=`uname -r`
if uname -r | grep -q '^2\.6\.28\>' ; then
  echo "WARNING: Your kernel version may suffer from kernel bug #12301, which"
  echo "prevents using the device after unbinding it! The bug is fixed in"
  echo "kernel version 2.6.29."
  echo
fi

find /sys/devices -name idVendor | \
  while read v
  do
    if [ "`cat $v`" = "$VID" ]
    then
      base="${v%/*}"
      pid="`cat $base/idProduct`"
      if echo $PIDS | grep -q "\\<$pid\\>"
      then
        echo "Found Yealink phone at $base"
        cd "$base"
        find . -name bInterfaceClass | \
          while read c
          do
            if [ "`cat $c`" = "03" ]
            then
              # this is the input interface
              d="${c%/*}"
              cd "$d"
              d="`pwd`"
              d="${d##*/}"
              echo "  found HID interface at $d"
              if [ -e "driver/unbind" ]
              then
                driver=`readlink driver`
                driver=${driver##*/}
                if [ "$driver" = "yealink" ]
                then
                  echo "  driver 'yealink' already attached - nothing to do"
                else
                  echo -n "$d" > driver/unbind
                  if [ $? = 0 ]
                  then
                    echo "  successfully detached driver '$driver'"
                    sleep 1
                    [ -f /sys/bus/usb/drivers/yealink/bind ] && \
                      echo -n "$d" > /sys/bus/usb/drivers/yealink/bind
                    if [ $? = 0 ]
                    then
                      echo "  successfully reattached driver 'yealink'"
                    else
                      echo "  could not reattach device to driver 'yealink'"
                    fi
                  else
                    echo "  could not detach driver '$driver'"
                  fi
                fi
              else
                echo "  there does not seem to be a driver attached"
              fi
            fi
          done
      fi
    fi
  done
