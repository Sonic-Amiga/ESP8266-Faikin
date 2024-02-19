#!/bin/sh

ISSUE_KIND=$1

if [ -z "$ISSUE_KIND" ]; then
	echo Required parameter is missing
	exit 255
fi

CHECK=$(shell git diff-index --name-status HEAD)
if [ -n "$CHECK" ]; then
	echo There are uncommitted changes in the repository:
	echo $CHECK
	exit 255
fi

make
cp Faikin-*.bin $ISSUE_KIND
git commit -a -m $ISSUE_KIND
git push
