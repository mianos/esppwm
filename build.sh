#!/usr/bin/env bash
set -e
export IDF_PATH=/Users/robertfowler/.espressif/v6.0.3/esp-idf
. "$IDF_PATH/export.sh" >/dev/null 2>&1
cd /Users/robertfowler/walocal/esppwm
idf.py build 2>&1 | tee /tmp/esppwm_build.log | tail -n 60
