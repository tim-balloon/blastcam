#!/bin/bash

# /home/starcam/github/blastcam/build/commands.sh -c 1 -s /dev/ttyLens1port8001 -p 8001 --verbose;

# Test flight only: a local commander that controls the software
/home/starcam/evanmayer/blastcam/commander/commander &

/home/starcam/evanmayer/blastcam/build/commands.sh -c 1 -s /dev/ttyLens1port8001 -p 8001 --verbose;
