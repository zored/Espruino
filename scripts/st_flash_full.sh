#!/bin/bash
echo ST-LINK flash
st-flash --reset write $1 0x08000000
