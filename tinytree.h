#ifndef _TINYTREE_H
#define _TINYTREE_H

/*
 * @date 2020-03-13 19:18:57
 *
 * `tinyTree_t` is a tree specifically designed for database generation.
 * It is speed optimised for `Xn9` datasets.
 *
 * Optimisations are:
 *   - Hardcoded `kstart` and `nstart`
 *   - Maximum of `X` nodes in `QTF` mode and `X*2` nodes in `QnTF` mode
 *   - Versioned memory
 *   - No endpoint/back-reference prefixing
 *   - Decoding assumes correct notations
 *   - Separate placeholder/skin
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
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include "context.h"
#include "datadef.h"

/**
 * Single unified operator node
 *
 * @typedef {object}
 * @date 2020-03-13 19:30:38
 */
struct tinyNode_t {
	/// @var {number} - reference to `"question"`
	uint32_t Q;
	/// @var {number} - reference to `"when-true"`. May have IBIT set
	uint32_t T;
	/// @var {number} - reference to `"when-false"`
	uint32_t F;
};

/**
 * High speed node tree
 *
 * @typedef {object}
 * @date 2020-03-13 19:31:48
 */
struct tinyTree_t {

	enum {
		/// @constant {number} - Number of nodes. Twice MAXSLOTS because of `QnTF` expansion
		TINYTREE_MAXNODES = (MAXSLOTS * 2),

		/// @constant {number} - Starting index in tree of first variable/endpoint
		TINYTREE_KSTART = 1,

		/// @constant {number} - Starting index in tree of first operator node
		TINYTREE_NSTART = (TINYTREE_KSTART + MAXSLOTS),

		/// @constant {number} - Total number of entries in tree
		TINYTREE_NEND = (TINYTREE_NSTART + TINYTREE_MAXNODES),

		/// @constant {number} - Maximum stack depth for tree walk. (3 operands + 1 opcode) per node
		TINYTREE_MAXSTACK = ((3 + 1) * TINYTREE_MAXNODES),

		/// @constant {number} - Maximum length of tree name. leaf + (3 operands + 1 opcode) per node + root-invert + terminator
		TINYTREE_NAMELEN = (1 + (3 + 1) * TINYTREE_MAXNODES + 1 + 1),
	};

	/// @var {number} functionality flags
	uint32_t flags;

	/// @var {number} index of first free node
	uint32_t count;

	/// @var {node_t[]} array of unified operators
	tinyNode_t N[TINYTREE_MAXNODES];

	// @var {number} single entrypoint/index where the result can be found
	uint32_t root;

	/**
	 * Constructor
	 *
	 * @param {number} flags - Tree/node functionality
 	 * @date 2020-03-14 00:27:38
	 */

	inline tinyTree_t(uint32_t flags) : flags(flags) {
		// only set flags because that determines tree functionality

		// assert that all nodes fit in a 32 bit vector (`beenWhere`)
		assert(TINYTREE_NEND < 32); // for beenThere
	}

	/*
	 * @date 2020-03-14 00:31:04
	 *
	 * Copy constructor not supported, so using them will trigger "unresolved externals"
	 */
	tinyTree_t(const tinyTree_t &rhs);
	tinyTree_t &operator=(const tinyTree_t &rhs);

	/**
	 * Erase the contents
	 *
	 * @date 2020-03-06 22:27:36
	 */
	inline void clear(void) {
		this->count = TINYTREE_NSTART; // rewind first free node
		this->root = 0; // set result to zero-reference
	}

	/**
	 * Perform level 1 normalisation on a `"Q,T,F"` triplet and add to the tree only when unique.
	 *
	 * Level 1 Normalisations include: inverting, function grouping, dyadic ordering and QnTF expanding.
	 *
	 * @param {number} Q
	 * @param {number} T
	 * @param {number} F
	 * @return {number} index into the tree pointing to a node with identical functionality. May have `IBIT` set to indicate that the result is inverted.
	 * @date 2020-03-13 19:34:52
	 */
	uint32_t normaliseQTF(uint32_t Q, uint32_t T, uint32_t F) {

		if (this->flags & context_t::MAGICMASK_PARANOID) {
			assert((Q & ~IBIT) < this->count);
			assert((T & ~IBIT) < this->count);
			assert((F & ~IBIT) < this->count);
		}

		/*
		 * Level 1a - Inverts
		 *
		 * ~q ?  t :  f  ->  q ? f : t
		 *  0 ?  t :  f  ->  f
		 *  q ?  t : ~f  ->  ~(q ? ~t : f)
		 */

		if (Q & IBIT) {
			// "~Q?T:F" -> "Q?F:T"
			uint32_t savT = T;
			T = F;
			F = savT;
			Q ^= IBIT;
		}
		if (Q == 0) {
			// "0?T:F" -> "F"
			return F;
		}

		// ibit indicates the result should be inverted
		uint32_t ibit = 0;

		if (F & IBIT) {
			// "Q?T:~F" -> "~(Q?~T:F)"
			F ^= IBIT;
			T ^= IBIT;
			ibit ^= IBIT;
		}

		/*
		 * Level 1b: Function grouping
		 *
		 * appreciated:
		 *
		 *  [ 0] a ? ~0 : 0  ->  a
		 *  [ 1] a ? ~0 : a  ->  a ? ~0 : 0
		 *  [ 2] a ? ~0 : b                  "+" or
		 *  [ 3] a ? ~a : 0  ->  0
		 *  [ 4] a ? ~a : a  ->  a ? ~a : 0
		 *  [ 5] a ? ~a : b  ->  b ? ~a : b
		 *  [ 6] a ? ~b : 0                  ">" greater-than
		 *  [ 7] a ? ~b : a  ->  a ? ~b : 0
		 *  [ 8] a ? ~b : b                  "^" xor/not-equal
		 *  [ 9] a ? ~b : c                  "!" QnTF
		 *
		 * depreciated:
		 *  [10] a ?  0 : 0 -> 0
		 *  [11] a ?  0 : a -> 0
		 *  [12] a ?  0 : b -> b ? ~a : 0    "<" less-than
		 *  [13] a ?  a : 0 -> a
		 *  [14] a ?  a : a -> a ?  a : 0
		 *  [15] a ?  a : b -> a ? ~0 : b
		 *  [16] a ?  b : 0                  "&" and
		 *  [17] a ?  b : a -> a ?  b : 0
		 *  [18] a ?  b : b -> b
		 *  [19] a ?  b : c                  "?" QTF
		 *
		 * ./eval --raw 'a00!' 'a0a!' 'a0b!' 'aa0!' 'aaa!' 'aab!' 'ab0!' 'aba!' 'abb!' 'abc!' 'a00?' 'a0a?' 'a0b?' 'aa0?' 'aaa?' 'aab?' 'ab0?' 'aba?' 'abb?' 'abc?'
		 */

		if (T & IBIT) {

			if (T == IBIT) {
				if (F == Q || F == 0) {
					// SELF
					// "Q?~0:Q" [1] -> "Q?~0:0" [0] -> Q
					return Q ^ ibit;
				} else {
					// OR
					// "Q?~0:F" [2]

					// level 1c: dyadic ordering
					if (Q > F) {
						// swap
						uint32_t savQ = Q;
						Q = F;
						F = savQ;
					}
				}
			} else if ((T & ~IBIT) == Q) {
				if (F == Q || F == 0) {
					// ZERO
					// "Q?~Q:Q" [4] -> "Q?~Q:0" [3] -> "0"
					return 0 ^ ibit;
				} else {
					// LESS-THAN
					// "Q?~Q:F" [5] -> "F?~Q:F" -> "F?~Q:0"
					Q = F;
					F = 0;
				}
			} else {
				if (F == Q || F == 0) {
					// GREATER-THAN
					// "Q?~T:Q" [7] -> "Q?~T:0" [6]
					F = 0;
				} else if ((T & ~IBIT) == F) {
					// XOR/NOT-EQUAL
					// "Q?~F:F" [8]

					// level 1c: dyadic ordering
					if (Q > F) {
						// swap
						uint32_t savQ = Q;
						Q = F;
						F = savQ;
						T = savQ ^ IBIT;
					}
				} else {
					// QnTF
					// "Q?~T:F" [9]
				}
			}

		} else {

			if (T == 0) {
				if (F == Q || F == 0) {
					// ZERO
					// "Q?0:Q" [11] -> "Q?0:0" [10] -> "0"
					return 0 ^ ibit;
				} else {
					// LESS-THAN
					// "Q?0:F" [12] -> "F?~Q:0" [6]
					T = Q ^ IBIT;
					Q = F;
					F = 0;
				}

			} else if (T == Q) {
				if (F == Q || F == 0) {
					// SELF
					// "Q?Q:Q" [14] -> Q?Q:0" [13] -> "Q"
					return Q ^ ibit;
				} else {
					// OR
					// "Q?Q:F" [15] -> "Q?~0:F" [2]
					T = 0 ^ IBIT;

					// level 1c: dyadic ordering
					if (Q > F) {
						// swap
						uint32_t savQ = Q;
						Q             = F;
						F             = savQ;
					}
				}
			} else {
				if (F == Q || F == 0) {
					// AND
					// "Q?T:Q" [17] -> "Q?T:0" [16]
					F = 0;

					// level 1c: dyadic ordering
					if (Q > T) {
						// swap
						uint32_t savQ = Q;
						Q             = T;
						T             = savQ;
					}
				} else if (T == F) {
					// SELF
					// "Q?F:F" [18] -> "F"
					return F ^ ibit;
				} else {
					// QTF
					// "Q?T:F" [19]
				}
			}
		}

		/*
		 * Directly before caching, rewrite `QTF` to `QnTF`
		 *
		 * a ?  b : c -> a?~(a?~b:c):c  "?" QTF
		 *
		 * ./eval --qntf 'ab&' 'abc?'
		 */

		if ((this->flags & context_t::MAGICMASK_QNTF) && (~T & IBIT)) {
			// QTF
			// Q?T:F -> Q?~(Q?~T:F):F)
			T = normaliseQTF(Q, T ^ IBIT, F) ^ IBIT;
		}

 		return this->basicNode(Q, T, F) ^ ibit;
	}

	/**
	 * Simple(fast) hash table lookup for nodes
	 *
	 * @param {number} Q
	 * @param {number} T
	 * @param {number} F
	 * @return {number} index into the tree pointing to a node with identical functionality. May have `IBIT` set to indicate that the result is inverted.
	 * @date 2020-03-13 19:57:04
	 */
	inline uint32_t basicNode(uint32_t Q, uint32_t T, uint32_t F) {

		// sanity checking
		if (this->flags & context_t::MAGICMASK_PARANOID) {
			assert(~Q & IBIT);                     // Q not inverted
			assert((T & IBIT) || (~this->flags & context_t::MAGICMASK_QNTF));
			assert(~F & IBIT);                     // F not inverted
			assert(Q != 0);                        // Q not zero
			assert(T != 0);                        // Q?0:F -> F?!Q:0
			assert(T != IBIT || F != 0);           // Q?!0:0 -> Q
			assert(Q != (T & ~IBIT));              // Q/T collapse
			assert(Q != F);                        // Q/F collapse
			assert(T != F);                        // T/F collapse
			assert((T & ~IBIT) != F || Q < F);     // NE ordering
			assert(F != 0 || (T & IBIT) || Q < T); // AND ordering
			assert(T != IBIT || Q < F);            // OR ordering
		}

		// test if component already exists
		for (uint32_t nid = TINYTREE_NSTART; nid < this->count; nid++) {
			const tinyNode_t *pNode = this->N + nid;
			if (pNode->Q == Q && pNode->T == T && pNode->F == F)
				return nid;
		}

		assert(this->count < TINYTREE_NEND);

		uint32_t   nid    = this->count++;
		tinyNode_t *pNode = this->N + nid;

		pNode->Q = Q;
		pNode->T = T;
		pNode->F = F;

		return nid;
	}

	/**
	 * decode error codes
	 * @date 2020-03-14 12:15:22
	 */
	enum {
		DERR_OK,                // success
		DERR_SYNTAX,            // unknown character in name
		DERR_PLACEHOLDER,       // placeholder not an lowercase endpoint
		DERR_OVERFLOW,          // stack overflow, might imply too big
		DERR_UNDERFLOW,         // stack underflow, notation not balanced
		DERR_INCOMPLETE,        // stuff still on stack. missing opcodes
		DERR_SIZE,              // notation too large for tree
	};

	/**
	 * Parse notation and construct tree accordingly.
	 * Notation is assumed to be normalised.
	 *
	 * Do not spend too much effort on detailing errors
	 *
	 * @param {string} pName - The notation describing the tree
	 * @param {string} pSkin - Skin
	 * @return non-zero when parsing failed
	 * @date 2020-03-13 21:30:32
	 */
	int decodeSafe(const char *pName, const char *pSkin = "abcdefghi") {

		// initialise tree
		this->count = TINYTREE_NSTART;
		this->root = 0;

		// state storage for postfix notation
		uint32_t stack[TINYTREE_MAXSTACK]; // there are 3 operands per per opcode
		int stackPos = 0;
		uint32_t beenThere[TINYTREE_NEND]; // track id's of display operators.
		uint32_t nextNode = TINYTREE_NSTART; // next visual node

		// walk through the notation until end or until placeholder/skin separator
		for (const char *pCh = pName; *pCh; pCh++) {

			if (isalnum(*pCh) && stackPos >= TINYTREE_MAXSTACK)
				return DERR_OVERFLOW;
			if (!isalnum(*pCh) && count >= TINYTREE_NEND - 1)
				return DERR_SIZE;
			if (islower(*pCh) && !islower(pSkin[*pCh - 'a']))
				return DERR_PLACEHOLDER;

			switch (*pCh) {
				case '0':
					stack[stackPos++] = 0;
					break;
				case 'a':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[0] - 'a');
					break;
				case 'b':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[1] - 'a');
					break;
				case 'c':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[2] - 'a');
					break;
				case 'd':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[3] - 'a');
					break;
				case 'e':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[4] - 'a');
					break;
				case 'f':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[5] - 'a');
					break;
				case 'g':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[6] - 'a');
					break;
				case 'h':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[7] - 'a');
					break;
				case 'i':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[8] - 'a');
					break;
				case '1':
					stack[stackPos++] = beenThere[nextNode - ('1' - '0')];
					break;
				case '2':
					stack[stackPos++] = beenThere[nextNode - ('2' - '0')];
					break;
				case '3':
					stack[stackPos++] = beenThere[nextNode - ('3' - '0')];
					break;
				case '4':
					stack[stackPos++] = beenThere[nextNode - ('4' - '0')];
					break;
				case '5':
					stack[stackPos++] = beenThere[nextNode - ('5' - '0')];
					break;
				case '6':
					stack[stackPos++] = beenThere[nextNode - ('6' - '0')];
					break;
				case '7':
					stack[stackPos++] = beenThere[nextNode - ('7' - '0')];
					break;
				case '8':
					stack[stackPos++] = beenThere[nextNode - ('8' - '0')];
					break;
				case '9':
					stack[stackPos++] = beenThere[nextNode - ('9' - '0')];
					break;

				case '>': {
					// GT (appreciated)
					if (stackPos < 2)
						return DERR_UNDERFLOW;

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = normaliseQTF(L, R ^ IBIT, 0);

					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '+': {
					// OR (appreciated)
					if (stackPos < 2)
						return DERR_UNDERFLOW;

					// pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = normaliseQTF(L, 0 ^ IBIT, R);

					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '^': {
					// XOR/NE (appreciated)
					if (stackPos < 2)
						return DERR_UNDERFLOW;

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = normaliseQTF(L, R ^ IBIT, R);

					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '!': {
					// QnTF (appreciated)
					if (stackPos < 3)
						return DERR_UNDERFLOW;

					// pop operands
					uint32_t F = stack[--stackPos];
					uint32_t T = stack[--stackPos];
					uint32_t Q = stack[--stackPos];

					// create operator
					uint32_t nid = normaliseQTF(Q, T ^ IBIT, F);

					// push
					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '&': {
					// AND (depreciated)
					if (stackPos < 2)
						return DERR_UNDERFLOW;

					// pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = normaliseQTF(L, R, 0);

					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '<': {
					// LT (obsolete)
					if (stackPos < 2)
						return DERR_UNDERFLOW;

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = normaliseQTF(L, 0, R);

					stack[stackPos++] = nid; // push
					beenThere[stackPos++] = nid; // save actual index for back references
					break;
				}
				case '?': {
					// QTF (depreciated)
					if (stackPos < 3)
						return DERR_UNDERFLOW;

					// pop operands
					uint32_t F = stack[--stackPos];
					uint32_t T = stack[--stackPos];
					uint32_t Q = stack[--stackPos];

					// create operator
					uint32_t nid = normaliseQTF(Q, T, F);

					stack[stackPos++] = nid; // push
					beenThere[nextNode++] = nid; // save actual index for back references
					break;
				}
				case '~': {
					// NOT (support)
					if (stackPos < 1)
						return DERR_UNDERFLOW;

					// invert top-of-stack
					stack[stackPos - 1] ^= IBIT;
					break;
				}

				case '/':
					// separator between placeholder/skin
					while (pCh[1])
						pCh++;
					break;
				case ' ':
					// skip spaces
					break;
				default:
					return DERR_SYNTAX;
			}
		}

		if (stackPos != 1)
			return DERR_INCOMPLETE;

		// store result into root
		this->root = stack[stackPos - 1];
		return DERR_OK;
	}

	/**
	 * Parse notation and construct tree accordingly.
	 * Notation is taken literally and not normalised
	 *
	 * WARNING: Does not check anything
	 *
	 * @param {string} pName - The notation describing the tree
	 * @param {string} pSkin - Skin
	 * @date 2020-03-13 21:11:04
	 */
	void decodeFast(const char *pName, const char *pSkin = "abcdefghi") {

		// initialise tree
		this->count = TINYTREE_NSTART;
		this->root = 0;

		// temporary stack storage for postfix notation
		uint32_t stack[TINYTREE_MAXSTACK]; // there are 3 operands per per opcode
		int stackPos = 0;

		// walk through the notation
		for (const char *pCh = pName; *pCh; pCh++) {

			switch (*pCh) {
				case '0':
					stack[stackPos++] = 0;
					break;
				case 'a':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[0] - 'a');
					break;
				case 'b':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[1] - 'a');
					break;
				case 'c':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[2] - 'a');
					break;
				case 'd':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[3] - 'a');
					break;
				case 'e':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[4] - 'a');
					break;
				case 'f':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[5] - 'a');
					break;
				case 'g':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[6] - 'a');
					break;
				case 'h':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[7] - 'a');
					break;
				case 'i':
					stack[stackPos++] = (uint32_t) (TINYTREE_KSTART + pSkin[8] - 'a');
					break;
				case '1':
					stack[stackPos++] = this->count - ('1' - '0');
					break;
				case '2':
					stack[stackPos++] = this->count - ('2' - '0');
					break;
				case '3':
					stack[stackPos++] = this->count - ('3' - '0');
					break;
				case '4':
					stack[stackPos++] = this->count - ('4' - '0');
					break;
				case '5':
					stack[stackPos++] = this->count - ('5' - '0');
					break;
				case '6':
					stack[stackPos++] = this->count - ('6' - '0');
					break;
				case '7':
					stack[stackPos++] = this->count - ('7' - '0');
					break;
				case '8':
					stack[stackPos++] = this->count - ('8' - '0');
					break;
				case '9':
					stack[stackPos++] = this->count - ('9' - '0');
					break;
				case '>': {
					// GT (appreciated)

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = L;
					this->N[nid].T = R ^ IBIT;
					this->N[nid].F = 0;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '+': {
					// OR (appreciated)

					// pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = L;
					this->N[nid].T = 0 ^ IBIT;
					this->N[nid].F = R;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '^': {
					// XOR/NE (appreciated)

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = L;
					this->N[nid].T = R ^ IBIT;
					this->N[nid].F = R;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '!': {
					// QnTF (appreciated)

					// pop operands
					uint32_t F = stack[--stackPos];
					uint32_t T = stack[--stackPos];
					uint32_t Q = stack[--stackPos];

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = Q;
					this->N[nid].T = T ^ IBIT;
					this->N[nid].F = F;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '&': {
					// AND (depreciated)

					// pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = L;
					this->N[nid].T = R;
					this->N[nid].F = 0;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '<': {
					// LT (obsolete)

					//pop operands
					uint32_t R = stack[--stackPos]; // right hand side
					uint32_t L = stack[--stackPos]; // left hand side

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = L;
					this->N[nid].T = 0;
					this->N[nid].F = R;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '?': {
					// QTF (depreciated)

					// pop operands
					uint32_t F = stack[--stackPos];
					uint32_t T = stack[--stackPos];
					uint32_t Q = stack[--stackPos];

					// create operator
					uint32_t nid = this->count++;
					this->N[nid].Q = Q;
					this->N[nid].T = T;
					this->N[nid].F = F;

					// push
					stack[stackPos++] = nid;
					break;
				}
				case '~': {
					// NOT (support)

					// invert top-of-stack
					stack[stackPos - 1] ^= IBIT;
					break;
				}
				case '/':
					// skip delimiter

					// store result into root
					this->root = stack[stackPos - 1];
					return;
			}
		}

		// store result into root
		this->root = stack[stackPos - 1];
	}

	/**
	 * Encode a notation describing the tree in "placeholder/skin" notation.
	 * Within the placeholders, endpoints are assigned in order of natural path which can be used as index for the skin to determine the actual endpoint.
	 *
	 * @param {number} id - entrypoint
	 * @param {boolean} withPlaceholders - true for "placeholder/skin" notation
	 * @return {string} Constructed notation. static storage so no multiple calls like with `printf()`.
	 * @date 2020-03-13 22:12:24
	 */
	const char * encode(uint32_t id, char *pSkin = NULL) {

		static char nameStorage[TINYTREE_NAMELEN + 1];

		char *pName = nameStorage;
		unsigned nameLen = 0;

		// temporary stack storage for postfix notation
		uint32_t stack[TINYTREE_MAXSTACK]; // there are 3 operands per per opcode
		int stackPos = 0;

		uint32_t nextNode;

		uint32_t beenThere = 0;
		uint32_t beenWhat[TINYTREE_NEND];

		if ((id & ~IBIT) < TINYTREE_NSTART) {
			if (pSkin) {
				if ((id & ~IBIT) == 0) {
					pName[nameLen++] = '0';
					pSkin[0] = 0;
				} else {
					pSkin[0] = 'a' + (id & ~IBIT) - TINYTREE_KSTART;
					pSkin[1] = 0;
					pName[nameLen++] = 'a';
				}

			} else {
				if ((id & ~IBIT) == 0) {
					pName[nameLen++] = '0';
				} else {
					pName[nameLen++] = 'a' + (id & ~IBIT) - TINYTREE_KSTART;
				}
			}

			// test for root invert
			if (id & IBIT)
				pName[nameLen++] = '~';

			// terminator
			pName[nameLen] = 0;

			return nameStorage;
		}

		/*
		 * For skins, walk the tree depth-first to enumerate the placeholders
		 */
		if (pSkin) {
			unsigned skinLen = 0;

			nextNode = TINYTREE_NSTART;

			stackPos = 0;
			stack[stackPos++] = id & ~IBIT;

			beenThere = (1 << 0); // set "been to zero"

			do {
				// pop stack
				uint32_t curr = stack[--stackPos];

				const tinyNode_t *pNode = this->N + curr;
				const uint32_t Q = pNode->Q;
				const uint32_t To = pNode->T & ~IBIT;
				const uint32_t Ti = pNode->T & IBIT;
				const uint32_t F = pNode->F;

				// determine if node already handled
				if (~beenThere & (1 << curr)) {
					/// first time

					// push id so it visits again a second time
					stack[stackPos++] = curr;

					if (Ti) {
						if (F == 0) {
							// GT Q?!T:0
							if (To >= TINYTREE_NSTART) stack[stackPos++] = To;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						} else if (To == 0) {
							// OR Q?!0:F
							if (F >= TINYTREE_NSTART) stack[stackPos++] = F;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						} else if (F == To) {
							// XOR Q?!F:F
							if (F >= TINYTREE_NSTART) stack[stackPos++] = F;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						} else {
							// QnTF Q?!T:F
							if (F >= TINYTREE_NSTART) stack[stackPos++] = F;
							if (To >= TINYTREE_NSTART) stack[stackPos++] = To;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						}
					} else {
						if (F == 0) {
							// AND Q?T:0
							if (To >= TINYTREE_NSTART) stack[stackPos++] = To;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						} else if (To == 0) {
							// LT Q?0:F
							if (F >= TINYTREE_NSTART) stack[stackPos++] = F;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						} else if (F == To) {
							// SELF Q?F:F
							assert(!"Q?F:F");
						} else {
							// QTF Q?T:F
							if (F >= TINYTREE_NSTART) stack[stackPos++] = F;
							if (To >= TINYTREE_NSTART) stack[stackPos++] = To;
							if (Q >= TINYTREE_NSTART) stack[stackPos++] = Q;
						}
					}

					// done, flag no endpoint assignment done
					beenThere |= (1 << curr);
					beenWhat[curr] = 0;

				} else if (beenWhat[curr] == 0) {
					// node complete, assign placeholders

					if (Q < TINYTREE_NSTART && (~beenThere & (1 << Q))) {
						beenThere |= (1 << Q);
						beenWhat[Q] = 'a' + skinLen;
						pSkin[skinLen++] = (char) ('a' + Q - TINYTREE_KSTART);
					}

					if (To < TINYTREE_NSTART && (~beenThere & (1 << To))) {
						beenThere |= (1 << To);
						beenWhat[To] = 'a' + skinLen;
						pSkin[skinLen++] = (char) ('a' + To - TINYTREE_KSTART);
					}

					if (F < TINYTREE_NSTART && (~beenThere & (1 << F))) {
						beenThere |= (1 << F);
						beenWhat[F] = 'a' + skinLen;
						pSkin[skinLen++] = (char) ('a' + F - TINYTREE_KSTART);
					}

					// flaq endpoints assigned
					beenWhat[curr] = nextNode++;
				}

			} while (stackPos > 0);

			assert(skinLen <= MAXSLOTS);
			pSkin[skinLen] = 0;
		}

		nextNode = TINYTREE_NSTART;

		stackPos = 0;
		stack[stackPos++] = id & ~IBIT;

		beenThere = (1 << 0); // re-walk the tree

		do {
			// pop stack
			uint32_t curr = stack[--stackPos];

			// if endpoint then emit
			if (curr < TINYTREE_NSTART) {
				if (curr == 0)
					pName[nameLen++] = '0'; // zero
				else if (!pSkin)
					pName[nameLen++] = 'a' + curr - TINYTREE_KSTART; // endpoint
				else
					pName[nameLen++] = (char) beenWhat[curr]; // placeholder

				continue;
			}

			const tinyNode_t *pNode = this->N + curr;
			const uint32_t Q = pNode->Q;
			const uint32_t To = pNode->T & ~IBIT;
			const uint32_t Ti = pNode->T & IBIT;
			const uint32_t F = pNode->F;

			// determine if node already handled
			if (~beenThere & (1 << curr)) {
				/// first time

				// push id so it visits again after expanding
				stack[stackPos++] = curr;
				// flaq no endpoint assignment done
				beenWhat[curr] = 0;

				if (Ti) {
					if (F == 0) {
						// GT Q?!T:0
						stack[stackPos++] = To;
						stack[stackPos++] = Q;
					} else if (To == 0) {
						// OR Q?!0:F
						stack[stackPos++] = F;
						stack[stackPos++] = Q;
					} else if (F == To) {
						// XOR Q?!F:F
						stack[stackPos++] = F;
						stack[stackPos++] = Q;
					} else {
						// QnTF Q?!T:F
						stack[stackPos++] = F;
						stack[stackPos++] = To;
						stack[stackPos++] = Q;
					}
				} else {
					if (F == 0) {
						// AND Q?T:0
						stack[stackPos++] = To;
						stack[stackPos++] = Q;
					} else if (To == 0) {
						// LT Q?0:F
						stack[stackPos++] = F;
						stack[stackPos++] = Q;
					} else if (F == To) {
						// SELF Q?F:F
						assert(!"Q?F:F");
					} else {
						// QTF Q?T:F
						stack[stackPos++] = F;
						stack[stackPos++] = To;
						stack[stackPos++] = Q;
					}
				}

				// done, flag no opcode done
				beenThere |= (1 << curr);
				beenWhat[curr] = 0;

			} else if (beenWhat[curr] == 0) {
				// node complete, append opcode

				if (Ti) {
					if (F == 0) {
						// GT Q?!T:0
						pName[nameLen++] = '>';
					} else if (To == 0) {
						// OR Q?!0:F
						pName[nameLen++] = '+';
					} else if (F == To) {
						// XOR Q?!F:F
						pName[nameLen++] = '^';
					} else {
						// QnTF Q?!T:F
						pName[nameLen++] = '!';
					}
				} else {
					if (F == 0) {
						// AND Q?T:0
						pName[nameLen++] = '&';
					} else if (To == 0) {
						// LT Q?0:F
						pName[nameLen++] = '<';
					} else if (F == To) {
						// SELF Q?F:F
						assert(!"Q?F:F");
					} else {
						// QTF Q?T:F
						pName[nameLen++] = '?';
					}
				}

				// flag opcode appended assigned
				beenWhat[curr] = nextNode++;

			} else {
				// back-reference to previous opcode

				uint32_t backref = nextNode - beenWhat[curr];
				assert(backref <= 9);
				pName[nameLen++] = '0' + backref;

			}

		} while (stackPos > 0);

		// test for root invert
		if (id & IBIT)
			pName[nameLen++] = '~';

		// terminator
		assert(nameLen <= TINYTREE_NAMELEN);
		pName[nameLen] = 0;

		return nameStorage;
	}

	/**
         * Evaluate the tree and store the result in v[]
         *
         * `this->N` contains the unified operators.
         * The parameter of this function `v` is the runtime data to which the operators should be applied.
         *
         * For each entry of `this->N[i]` and `v[i]`:
         * - the unified operator loads the operand data from `v` indicated by `Q`, `T` and `F`
         * - functionality is applied
         * - the result is stored int `v` indexed by the location of the operator
         *
         * Each data entry is a 512-bit wide vector, split into uint64_t chunks.
         * As this is a reference implementation, `SIMD` instructions should be avoided.
         *
	 * @param {vector[]} v - the evaluated result of the unified operators
	 * @date 2020-03-09 19:36:17
	 */
	inline void eval(footprint_t *v) const {
		// for all operators eligible for evaluation...
		for (uint32_t i = TINYTREE_NSTART; i < this->count; i++) {
			// point to the first chunk of the `"question"`
			const uint64_t *Q = v[this->N[i].Q].bits;
			// point to the first chunk of the `"when-true"`
			// NOTE: this can be marked as "value needs be runtime inverted"
			const uint64_t *T = v[this->N[i].T & ~IBIT].bits;
			// point to the first chunk of the `"when-false"`
			const uint64_t *F = v[this->N[i].F].bits;
			// point to the first chunk of the `"result"`
			uint64_t *R = v[i].bits;

			// determine if the operator is `QTF` or `QnTF`
			if (this->N[i].T & IBIT) {
				// `QnTF` for each bit in the chunk, apply the operator `"Q ? !T : F"`
				for (unsigned j = 0; j < footprint_t::QUADPERFOOTPRINT; j++)
					R[j] = (Q[j] & ~T[j]) ^ (~Q[j] & F[j]);
			} else {
				// `QnTF` for each bit in the chunk, apply the operator `"Q ? T : F"`
				for (unsigned j = 0; j < footprint_t::QUADPERFOOTPRINT; j++)
					R[j] = (Q[j] & T[j]) ^ (~Q[j] & F[j]);
			}
		}
	}

	/**
	 * Create an initial data vector for the evaluator
	 *
	 * During evaluation there are number of states values can possible take.
	 * For expressions with 9 input variables there are `2^9=512` possible value states
	 *
	 * Using 512 bit vectors, it is possible to associate each bit position with a value state.
	 * When evaluating the tree, all 512 bits of the vector can be performed in parallel.
	 *
	 * Example expression stored in data-vector (`v[]`) and tree (`N[]`)
	 *
	 *    index     | v[]        |  N[]
	 * -------------+------------+--------
	 * [0]          | 0b00000000 | null/zero/false
	 * [1=KSTART+0] | 0b10101010 | `a`
	 * [2=KSTART+1] | 0b11001100 | `b`
	 * [3=KSTART+2] | 0b11110000 | `c`
	 * [4=NSTART+0] | 0b10001000 | `ab&`
	 * [5=NSTART+1] | 0b01111000 | `ab&c^`
	 *
	 * In most cases the trees to be evaluated have explicit skins.
	 * This has effect on how input variables are preloaded into the vector.
	 *
	 *    index     | v[]        |  N[]
	 * -------------+------------+--------
	 * [0]          | 0b00000000 | null/zero/false
	 * [1=KSTART+0] | 0b11001100 | `b`
	 * [2=KSTART+1] | 0b11110000 | `c`
	 * [3=KSTART+2] | 0b10101010 | `a`
	 * [4=NSTART+0] | 0b11000000 | `ab&`/bca
	 * [5=NSTART+1] | 0b01101010 | `ab&c^`/bca
	 *
	 * In the above table, placeholders in `ab&` directly index into `v[]`, and skin `/bca` is how endpoint `v[]` are preloaded.
	 *
	 * Encoding the initial state for `v[]` effected by skins is CPU-expensive.
	 *
	 * To optimise this, not one but 9! vectors are preloaded, each with appropiate endpoint values for each transform permutation.
	 *
	 * For `tinyTree_t` with `MAXNODE` set to 10, the size of the complete data vector is:
	 * 	`(sizeof(footprint_t)*(1+MAXSLOTS+MAXNODE)*MAXSLOTS!)`=(64*(1+9+10)*9!)=464 Mbyte.
	 * And there are 2 data vectors, one for forward transforms and a second for reverse.
	 *
	 * @param {footprint_t) pFootprint - footprint to initialise
	 * @param {footprint_t) maxTransform - How many transforms
	 * @param {uint64_t[]) pTransformData - forward or reverse transform data
	 * @date 2020-03-15 15:39:59
	 */
	void initialiseVector(context_t &ctx, footprint_t *pFootprint, uint32_t numTransform, uint64_t *pTransformData) {

		// hardcoded assumptions
		assert(MAXSLOTS == 9);

		/*
		 * The patterns and generators have a hardcoded conceptual assumption about the following:
		 */
		uint64_t *v = (uint64_t *) pFootprint;

		// zero everything
		::memset(pFootprint, 0, sizeof(*pFootprint) * TINYTREE_NEND * numTransform);

		/*
		 * Initialize the data structures
		 */
		ctx.tick = 0;
		for (unsigned iTrans = 0; iTrans < MAXTRANSFORM; iTrans++) {

			if (ctx.opt_verbose >= ctx.VERBOSE_TICK && ctx.tick) {
				fprintf(stderr, "\r\e[K%.5f%%", iTrans * 100.0 / MAXTRANSFORM);
				ctx.tick = 0;
			}

			// set 64bit slice to zero
			for (unsigned i = 0; i < footprint_t::QUADPERFOOTPRINT * TINYTREE_NSTART; i++)
				v[i] = 0;

			// set footprint for 64bit slice
			for (unsigned i = 0; i < (1 << MAXSLOTS); i++) {

				// binary transform name. Each nibble is unique
				uint64_t transformMask = *pTransformData;

				// v[(i/64)+0*4] should be 0
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 1 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 2 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 3 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 4 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 5 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 6 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 7 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 8 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
				transformMask >>= 4;
				if (i & (1LL << (transformMask & 15)))
					v[(i / 64) + 9 * footprint_t::QUADPERFOOTPRINT] |= 1LL << (i % 64);
			}

			v += footprint_t::QUADPERFOOTPRINT * TINYTREE_NEND;
			pTransformData++;
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");
	}

};

#endif