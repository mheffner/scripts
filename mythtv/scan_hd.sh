#!/bin/bash

freq_file=$(mktemp /tmp/frequency.XXXX)
cat /usr/share/dvb-apps/atsc/*QAM256* | sed "/^$/d; /^#/d" | sort -u \
	> $freq_file

chanconf=$(mktemp /tmp/channels.conf.XXXX)
tmp=$(mktemp /tmp/ch2.XXXX)

echo -n "Outputing to $chanconf..."
scandvb $freq_file > $chanconf
echo "Done"

sed "s|^[^:]*||" $chanconf | cat -n | sed "s|\s||g" > $tmp

mv $tmp $chanconf

IFS='
'
old="channels.conf.`date +%Y-%m-%d-%T`"
if [ -e ~/.mplayer/channels.conf ]; then
	mv ~/.mplayer/channels.conf ~/.mplayer/$old
fi

# Remove the echo frequencies
cat $chanconf | grep -v '012500:QAM_256:' > ~/.mplayer/channels.conf

for f in $(cat $chanconf | awk -F \: '{print $1}'); do
	echo "-----------------------"
	echo "Now processing $f"
	echo "-----------------------"
	mencoder \
		dvb://$f \
		-nosound \
		-frames 10 \
		-ovc lavc \
		-o $f.avi & 
	for i in 1 2 3 4 5 6 7 8 9 10
	do
	  killall -0 mencoder > /dev/null 2>&1
	  if [ $? -eq 1 ]; then
	      break;
	  fi
	  sleep 1
	done
	killall mencoder

	sleep 1
	killall -9 mencoder
done

tmp=$(mktemp /tmp/channels.conf.XXXX)
chan_grep=$(echo $(ls *.avi) | \
	sed "s:\.avi::g; s:\<[0-9]\+\>:^\\\<&\\\>:g; s: :\\\|:g")

grep "$chan_grep" ~/.mplayer/channels.conf > $tmp
mv $tmp ~/.mplayer/channels.conf

if [ ! -e ~/.mplayer/$old ]; then
    exit
fi

# Use original channels.conf line for channels we already know
tmp=$(mktemp /tmp/channels.conf.XXXX)
for line in $(cat $HOME/.mplayer/channels.conf); do
    FREQ=$(echo $line | awk -F ':' '{print $2}')
    CHAN=$(echo $line | awk -F ':' '{print $6}')

    HAVE=$(egrep '.*:'"$FREQ"':QAM_256:.*:'"$CHAN"'$' ~/.mplayer/$old)
    if [ ! -z "$HAVE" ]; then
	echo "$HAVE" >> $tmp
    else
	echo "$line" >> $tmp
    fi
done
mv $tmp ~/.mplayer/channels.conf

#mv ~/.mplayer/$old ~/.mplayer/channels.conf
