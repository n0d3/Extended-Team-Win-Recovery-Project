#!/sbin/sh
# checksys.sh

output=`grep -i "clk" /proc/cmdline`

if [ -n "$output" ]
then
  echo "clk=true" > /tmp/nfo.prop
else
  echo "clk=null" > /tmp/nfo.prop
fi