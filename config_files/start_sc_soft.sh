#!/bin/bash

# For the star camera computer startup, rename the serial port the lens
# controller is attached to:
ln -s /dev/ttyS4 /dev/ttyLens1port8001;

# Test flight only: a local commander that controls the software
/home/starcam/evanmayer/blastcam/commander/commander &

# /home/starcam/github/blastcam/build/commands.sh -c 1 -s /dev/ttyLens1port8001 -p 8001 --verbose;
/home/starcam/evanmayer/blastcam/build/commands.sh -c 1 -s /dev/ttyLens1port8001 -p 8001 --verbose;
