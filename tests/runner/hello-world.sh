#!/usr/bin/bash

# Check if an argument is provided
if [ -z "$1" ]; then
    echo "Usage: $0 <upper_bound>"
    exit 1
fi

upper_bound=$1

# Dynamically generate the range
for i in $(eval echo {1..$upper_bound}); do
    echo "Hello World ($i/$upper_bound)"
    sleep 3
done
