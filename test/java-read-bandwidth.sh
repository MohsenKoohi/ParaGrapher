#!/bin/bash

fc="sudo /drop_caches.sh"
path=$1
echo "Path: "$path

threads="1 18 36"
blocksizes="4096 `echo 4096*1024|bc`"
filesize=`echo 4096*1024*36*4| bc`

make ReadBandwidth args="-p /storage-ssd/temp  -s $filesize"

echo -e "\n"
echo "mmap (MB/s)"
for t in $threads; do
	$fc
	echo "$t;"`java -ea -cp obj: ReadBandwidth -p $path  -s $filesize -t $t mmap`
done

echo -e "\n"
echo "read (MB/s)"
for t in $threads; do
	for b in $blocksizes; do 
		$fc
		echo "$t; $b;" `java -ea -cp obj: ReadBandwidth -p $path  -s $filesize -t $t -b $b read`
	done
done

$fc