#!/bin/sh
pgrep -al vhal_proxy | awk '
/cvd/ { print $NF }
'
