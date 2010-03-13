#! /usr/bin/env python

# =============================================================================
# File: depend_filter.py
#
# Copyright (C) 2007 Nokia. All rights reserved.
#
# Author: Simo Piiroinen <simo.piiroinen@nokia.com>
#
# -----------------------------------------------------------------------------
#
# History:
#
# 05-Dec-2007 Simo Piiroinen
# - initial version
# =============================================================================

# gcc -MM filters out only standard includes, which
# does not cover glib etc headers ... so we filter
# out all dependencies with absolute path

import sys,os

DEST = None

args = sys.argv[1:]
args.reverse()
while args:
    a = args.pop()
    k,v = a[:2],a[2:]
    if k in "-d":
        DEST = v or args.pop()
    else:
        print>>sys.stderr, "Unknown option: %s" % a
        sys.exit(1)

def dep_compare(a,b):
    return cmp(a.count("/"),b.count("/")) or cmp(a,b)

def dep_filter(deps):
    src, hdr = [], {}

    for dep in deps:
        if dep.endswith(".c"):
            src.append(dep)
        elif dep.startswith("/"):
            continue
        elif not dep in hdr:
            hdr[dep] = None
    hdr = hdr.keys()
    hdr.sort(dep_compare)
    return src + hdr

for line in sys.stdin.read().replace("\\\n", " ").split("\n"):
    if not ':' in line:
        continue
    dest,srce = line.split(":",1)

    if DEST:
        dest = os.path.basename(dest)
        dest = os.path.join(DEST, dest)

    srce = dep_filter(srce.split())
    print '%s: %s\n' % (dest, " \\\n  ".join(srce))
