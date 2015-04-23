#!/bin/bash

SPEED=100
DELAY=1
LOSS=0
CORRUPT=$1

killall link 2> /dev/null
killall server 2> /dev/null
killall client 2> /dev/null

./link_emulator/link speed=$SPEED delay=$DELAY loss=$LOSS corrupt=$CORRUPT &> /dev/null &
sleep 1
./server $2 &
sleep 1

./client $2
