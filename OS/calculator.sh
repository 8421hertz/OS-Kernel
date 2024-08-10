#!/bin/bash
echo | awk "{printf(\"%${2}s\n\", \"$1\")}"
