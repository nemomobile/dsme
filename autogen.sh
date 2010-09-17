#!/bin/sh
#
# Bootstrap autotools environment
libtoolize
aclocal
automake -ac
autoconf
echo 'Ready to rock!'
