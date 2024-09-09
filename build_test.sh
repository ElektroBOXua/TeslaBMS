gcc tesla_bms.test.c -std=c99 -Wall -Wextra

if diff -u --strip-trailing-cr --color=always good_output.txt <(./a); then
	echo "Test passed: Output matches expected output."
else
	echo "Test failed: Output differs from expected output."
fi
