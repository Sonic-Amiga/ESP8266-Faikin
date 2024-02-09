#!/bin/sh

CHECK=$(shell git diff-index --name-status HEAD)
if [ -n "$CHECK" ]; then
	echo There are uncommitted changes in the repository:
	echo $CHECK
	exit 255
fi

make
cp Faikin-*.bin release
git commit -a -m release
git push
