#!/bin/bash

rm -rf 1
rm client_output
mkdir 1
touch 1/1
touch 1/2
touch 1/3
touch 1/4
echo "cd 1
ls .
exit exit
" > commands

./run_experiment.sh $1 $2

DIFF=$(diff client_output test1) 
if [ "$DIFF" != "" ] 
then
    echo "FAIL"
else
	echo "PASS"
fi

