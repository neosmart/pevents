#!/bin/sh

for f in $(rg . -l -t cpp); do
	clang-format $f | rewrite $f
done
