#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wcwidth.h"

static int tests_run;
static int test_failures;

void assertWidthIs(int expected_width, uint32_t c) {
	tests_run++;
        int actual_width = tsm_ucs4_wcwidth(c);
        if (actual_width != expected_width) {
                fprintf(stderr, "ERROR: tsm_ucs4_wcwidth(U+%04x) returned %d, expected %d\n", c, actual_width, expected_width);
		test_failures++;
        }
}

int main() {
        assertWidthIs(1, 'a');
        assertWidthIs(1, L'ö');

	// Some wide:
	assertWidthIs(2, L'Ａ');
	assertWidthIs(2, L'Ｂ');
	assertWidthIs(2, L'Ｃ');
	assertWidthIs(2, L'中');
	assertWidthIs(2, L'文');
	assertWidthIs(2, 0x679C);
	assertWidthIs(2, 0x679D);
	assertWidthIs(2, 0x2070E);
	assertWidthIs(2, 0x20731);

	assertWidthIs(1, 0x11A3);

	// Powerline and similar private-use glyphs are single-cell in patched terminal fonts.
	assertWidthIs(1, 0xE0B0);
	assertWidthIs(1, 0xE0B1);
	assertWidthIs(1, 0xE0B4);
	assertWidthIs(1, 0xE0B5);
	assertWidthIs(1, 0xE0B6);
	assertWidthIs(1, 0xE0B7);
	assertWidthIs(1, 0xE0B8);
	assertWidthIs(1, 0xE0B9);
	assertWidthIs(1, 0xE0BA);
	assertWidthIs(1, 0xE0BC);
	assertWidthIs(1, 0xE0BD);
	assertWidthIs(1, 0xE0BE);

	// Symbols for Legacy Computing used as cell graphics should remain single-cell.
	assertWidthIs(1, 0x1FB98);
	assertWidthIs(1, 0x1FB99);
	assertWidthIs(1, 0x1FB9C);
	assertWidthIs(1, 0x1FB9D);
	assertWidthIs(1, 0x1FB9E);

	// Segmented digits are single-cell symbols and should not be treated as wide.
	assertWidthIs(1, 0x1FBF0);
	assertWidthIs(1, 0x1FBF1);
	assertWidthIs(1, 0x1FBF2);
	assertWidthIs(1, 0x1FBF3);
	assertWidthIs(1, 0x1FBF4);
	assertWidthIs(1, 0x1FBF5);
	assertWidthIs(1, 0x1FBF6);
	assertWidthIs(1, 0x1FBF7);
	assertWidthIs(1, 0x1FBF8);
	assertWidthIs(1, 0x1FBF9);

	assertWidthIs(2, 0x1F428); // Koala emoji.
        assertWidthIs(2, 0x231a);  // Watch emoji.

	if (test_failures > 0) printf("%d tests FAILED, ", test_failures);
	printf("%d tests OK\n", tests_run);
	return (test_failures == 0) ? 0 : 1;
}

