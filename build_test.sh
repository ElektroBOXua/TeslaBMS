gcc tesla_bms.test.c -std=c99 -Wall -Wextra

./a > output.txt

if git --no-pager diff -u --word-diff --color=always \
       --no-index good_output.txt output.txt; then
	echo "Test passed: Output matches expected output."
else
	echo "Test failed: Output differs from expected output."
fi
