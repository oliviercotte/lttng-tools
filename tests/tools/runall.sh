#!/bin/bash

DIR=$(dirname $0)

tests=( $DIR/test_kernel_data_trace $DIR/test_sessions $DIR/test_ust_data_trace )
exit_code=0

function start_tests ()
{
    for bin in ${tests[@]};
    do
        ./$bin
        # Test must return 0 to pass.
        if [ $? -ne 0 ]; then
            exit_code=1
            break
        fi
    done
}

start_tests

exit $exit_code
