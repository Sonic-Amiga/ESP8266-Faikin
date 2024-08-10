#!/bin/sh

ISSUE_KIND=$1

if [ -z "$ISSUE_KIND" ]; then
	echo Required parameter is missing
	exit 255
fi

# Workaround: On Windows we have transparent CRLF translation, whose rules
# may conflict between TortoiseGIT and command-line mingw git. So we may
# end up in false "changed" status on files just because of that. It turns out
# running 'git status' somehow fixes that.
git status >/dev/null

CHECK=$(shell git diff-index --name-status HEAD)
if [ -n "$CHECK" ]; then
	echo There are uncommitted changes in the repository:
	echo "$CHECK"
	exit 255
fi

make

echo 8266 $ISSUE_KIND > .release-commit.tmp
echo >> .release-commit.tmp
echo -n "libRevK: " >> .release-commit.tmp
git rev-parse --short @:./components/ESP32-RevK >> .release-commit.tmp
./parse_appdesc.py Faikin-8266.desc >> .release-commit.tmp
echo >> .release-commit.tmp

if [ "$?" != "0" ]; then
    echo Error parsing revision!
	exit 255
fi

cp Faikin-*.bin $ISSUE_KIND
cp Faikin-*.desc $ISSUE_KIND

git commit -a -t .release-commit.tmp --signoff
git push

# If we don't save our commit message, after editing, commit is aborted, and push
# would not publish anything. For convenience we now revert our issue directory to
# HEAD. If commit succeeded, it would do nothing because the new commit is now HEAD
git checkout HEAD $ISSUE_KIND
