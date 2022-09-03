#!/bin/sh
# yes | valgrind --suppressions=suppressions.txt --gen-suppressions=yes --leak-check=full --show-leak-kinds=all "$@"
yes | valgrind --leak-check=full --show-leak-kinds=all "$@"
