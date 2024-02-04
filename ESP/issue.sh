#!/bin/sh

CHECK=$(shell git diff-index --name-status HEAD)
if [ -n "$CHECK" ]; then
	echo There are uncommitted changes in the repository:
	echo $CHECK
	exit 255
fi

git pull
git submodule update --recursive
git commit -a -m checkpoint
@make
cp $(PROJECT_NAME)*.bin release
git commit -a -m release
git push
