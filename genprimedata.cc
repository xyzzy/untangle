/*
 * @date 2020-03-14 18:11:10
 *
 * `genprimedata` generates prime numbers with 1M interval.
 *
 * Using optimised Sieve of Eratosthenes
 *
 * Database indices are hashed table lookups with overflow.
 * Their sizes need to be a prime number for `"id % tableSize"` to work
 *
 * The output of this program is used to raise index sizes to the next largest prime.
 */

/*
 *	This file is part of Untangle, Information in fractal structures.
 *	Copyright (C) 2017-2020, xyzzy@rockingship.org
 *
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

/// @constant {number} - Largest prime to fit uint32_t
#define MAXPRIME 0xffffffff

/// @constant {number} - Snap interval
#define BUMP 1000000

/// @global {boolean[]} - true if index is a prime
uint8_t *isPrime;

/// @global {number} - async indication that a timer interrupt occurred
unsigned tick;

/**
 * Construct a time themed prefix string for console logging
 *
 * @date 2020-03-12 13:37:12
 */
const char *timeAsString(void) {
	static char tstr[256];

	time_t t = time(0);
	struct tm *tm = localtime(&t);
	strftime(tstr, sizeof(tstr), "%F %T", tm);

	return tstr;
}

/**
 * Signal handlers
 *
 * Bump interval timer
 *
 * @param {number} sig - signal (ignored)
 * @date 2020-03-14 18:11:59
 */
void sigalrmHandler(int sig) {
	(void) sig; // trick compiler t see parameter is used

	tick++;
	alarm(1);
}

/**
 * Program main entry point
 *
 * @param  {number} argc - number of arguments
 * @param  {string[]} argv - program arguments
 * @return {number} 0 on normal return, non-zero when attention is required
 * @date   2020-03-14 18:12:59
 */
int main(int argc, char *const *argv) {
	setlinebuf(stdout);

	/*
	 * Test if output is redirected
	 */
	if (isatty(1)) {
		fprintf(stderr, "stdout not redirected\n");
		exit(1);
	}

	/*
	 * register timer handler
	 */
	signal(SIGALRM, sigalrmHandler);
	::alarm(1);

	/*
	 * Allocate and prepare vector marking primes
	 */

	fprintf(stderr, "\r\e[K[%s] Allocating\n", timeAsString());

	isPrime = (uint8_t *) malloc(sizeof(*isPrime) * MAXPRIME);
	assert(isPrime);

	for (uint64_t i = 2; i < MAXPRIME; i++)
		isPrime[i] = i % 2;
	isPrime[0] = 0;
	isPrime[1] = 0;

	/*
	 * Perform optimised Sieve of Eratosthenes
	 */
	unsigned numPrimes = 1; // 2 is predefined
	uint64_t numProgress = 0;
	uint64_t progressHi = 9108448263;

	for (uint64_t iPrime = 3; iPrime * iPrime < MAXPRIME; iPrime++) {
		if (tick) {
			// timer interval detected
			fprintf(stderr, "\r\e[K[%s] %.1f%%", timeAsString(), numProgress * 100.0 / progressHi);

			tick = 0;
		}

		if (isPrime[iPrime]) {
			numPrimes++; // increment number of primes
			for (uint64_t j = iPrime * iPrime; j < MAXPRIME; j += iPrime) {
				isPrime[j] = false;
				numProgress++; // increment progress
			}
		}
	}
	fprintf(stderr, "\r\e[K"); // erase line

	if (numProgress != progressHi)
		fprintf(stderr, "WARNING: progressHi not %ld\n", numProgress);

	/*
	 * Write to stdout
	 */

	printf("// generated by %s on \"%s\"\n", argv[0], timeAsString());
	printf("\n");
	printf("#ifndef _PRIMEDATA_H\n");
	printf("#define _PRIMEDATA_H\n");
	printf("\n");
	printf("#include <stdint.h>\n");
	printf("\n");

	printf("uint32_t primeData[] = {\n");

	unsigned numSelected = 0;
	unsigned column = 0;
	uint64_t bump = BUMP;

	for (uint64_t iPrime = 3; iPrime < MAXPRIME; iPrime += 2) {
		if (iPrime > bump && isPrime[iPrime]) {
			printf("%9lu,", iPrime);
			numSelected++;

			if (column++ % 16 == 15)
				printf("\n");
			bump += BUMP;
		}
	}

	// last entry and trailer
	printf("0xffffffff};\n");
	printf("\n");
	printf("#endif\n");

	// status
	fprintf(stderr, "[%s] Selected %u primes\n", timeAsString(), numSelected);

	return 0;
}
