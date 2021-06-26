//# pragma GCC optimize ("O0") // optimize on demand

/*
 * @date 2020-03-30 17:20:21
 *
 * Collect signature group members.
 *
 * Basic group members share the same node size, which is the smallest a signature group can have.
 * A member is considered safe if the three components and heads all reference safe members.
 * Some groups are unsafe. Replacements are found by selecting larger structures.
 *
 * Keep smaller unsafe nodes for later normalisations.
 *
 * normalisation:
 * 1) Algebraic (function grouping)
 * 2) Dyadic ordering (layout ordering)
 * 3) Imprints (layout orientation "skins")
 * 4) Signature groups (restructuring)
 * Basically, `genmember` collects structures that do not trigger normalisation or orphans when used for creation/construction.
 *
 * @date 2020-04-01 23:48:02
 *
 * I always thought that the goal motivation was to replace structures with smallest nodesize but that might not be the case.
 * 3040 signature groups in 4n9 space fail to have safe members. However, they do exist in 5n9 space.
 *
 * @date 2020-04-02 10:44:05
 *
 * Structures have heads and tails.
 * Tails are components and sub-components, heads are the structures minus one node.
 * Safe members have safe heads and tails.
 * Size of signature group is size of smallest safe member.
 *
 * @date 2020-04-02 23:43:18
 *
 * Unsafe members start to occur in 4n9 space, just like back-references.
 *
 * @date 2020-04-06 22:55:07
 *
 * `genmember` collects raw members.
 * Invocations are made with increasing nodeSize to find new members or safe replacements.
 * Once a group is safe (after invocation) new members will be rejected, this makes that only unsafe groups need detection.
 * Multi-pass is possible by focusing on a a smaller number of signature groups. This allows for extreme high speeds (interleave) at a cost of storage.
 * `genmember` actually needs two modes: preparation of an imprint index (done by master) and collecting (done by workers).
 * Workers can take advantage of the read-only imprint index in shared memory (`mmap`)
 *
 * Basically, `genmember` collects constructing components.
 * Only after all groups are safe can selecting occur.
 *
 * - All single member groups lock components (tails) and providers (heads)
 * - Groups with locked heads and tails become locked themselves.
 * Speculating:
 * - unsafe members can be grouped by component sid (resulting in a single "best `compare()`" member
 * - safe members can be grouped by component mid (resulting in a single "best `compare()`" member
 * - unsafe groups with locked members but unsafe providers can promote the providers (what to do when multiple)
 * - safe groups with unsafe members can release heads/tails allowing their refcount to drop to zero and be removed.
 *
 * Intended usage:
 *
 * - prepare new database by creating imprints for safe members.
 *   It is safe to use extreme high interleave (5040, 15120, 40320 and 60480)
 *   The higher the faster but less groups to detect.
 *
 * - After prepare let workers collect members using `--text=3` which collects on the fly.
 *
 * - After all workers complete, join all worker results and create dataset, use `--text=1`
 *
 * - repeat preparing and collecting until collecting has depleted
 *
 * - increase nodeSize by one and repeat.
 *
 * NOTE: don't be smart in rejecting members until final data-analysis is complete.
 *       This is a new feature for v2 and uncharted territory
 *
 * @date 2020-04-07 01:07:34
 *
 * At this moment calculating and collecting:
 * `restartData[]` for `7n9-pure`. This is a premier!
 * signature group members for 6n9-pure. This is also premier.
 *
 * pure dataset looks promising:
 * share the same `4n9` address space, which holds 791646 signature groups.
 * `3n9-pure` has 790336 empty and 0 unsafe groups
 * `4n9-pure` has 695291 empty and 499 unsafe groups
 * `5n9-pure` has .. empty and .. unsafe groups
 * now scanning `6n9-pure` for the last 46844.
 * that is needs to get as low as possible, searching `7n9` is far above my resources.
 * Speed is about 1590999 candidates/s
 *
 * The pure dataset achieves the same using only `"Q?!T:F"` / `"abc!"` nodes/operators.
 * This releases the requirement to store information about the inverted state of `T`.
 * `T` is always inverted.
 * To compensate for loss of variety more nodes are needed.
 *
 * safe members avoid being normalised when their notation is being constructed.
 * From the constructor point of view:
 *   unsafe members have smaller nodeSize but their notation is written un a language not understood
 *   it can be translated with penalty (extra nodes)
 *
 * @date 2020-04-07 20:57:08
 *
 * `genmember` runs in 3 modes:
 * - Merge (default)
 *   = Signatures are copied
 *   = Imprints are inherited or re-built on demand
 *   = Members are copied
 *   = Additional members are loaded/generated
 *   = Member sorting
 *
 * - Prepare
 *   = Signatures are copied
 *   = Imprints are set to select empty=unsafe signature groups
 *   = Members are inherited
 *   = No member-sorting
 *   = Output is intended for `--mode=merge`
 *
 * - Collect (worker)
 *   = Signatures are copied
 *   = Imprints are inherited
 *   = Members are inherited
 *   = Each candidate member that matches is logged, signature updated and not recorded
 *   = No member-sorting
 *
 * @date 2020-04-22 21:20:56
 *
 * `genmember` selects candidates already present in the imprint index.
 * Selected candidates are added to `members`.
 *
 * @date 2020-04-22 21:37:03
 *
 * Text modes:
 *
 * `--text[=1]` Brief mode that show selected candidates passed to `foundTreeSignature()`.
 *              Selected candidates are those that challenge and win the current display name.
 *              Also intended for transport and merging when broken into multiple tasks.
 *              Can be used for the `--load=<file>` option.
 *              Output can be converted to other text modes by `"./gensignature <input.db> <numNode> [<output.db>]--load=<inputList> --no-generate --text=<newMode> '>' <outputList>
 *
 *              <name>
 *
 * `--text=2`   Full mode of all candidates passed to `foundTreeSignature()` including what needed to compare against the display name.
 *
 *              <cid> <sid> <cmp> <name> <size> <numPlaceholder> <numEndpoint> <numBackRef>

 *              where:
 *                  <cid> is the candidate id assigned by the generator.
 *                  <sid> is the signature id assigned by the associative lookup.
 *                  <cmp> is the result of `comparSignature()` between the candidate and the current display name.
 *
 *              <cmp> can be:
 *                  cmp = '<'; // worse, group safe, candidate unsafe
 *                  cmp = '-'; // worse, candidate too large for group
 *                  cmp = '='; // equal, group unsafe, candidate unsafe
 *                  cmp = '+'; // equal, group safe, candidate safe
 *                  cmp = '>'; // better, group unsafe, candidate safe
 *
 * `--text=3`   Selected and sorted signatures that are written to the output database.
 *              NOTE: same format as `--text=1`
 *              NOTE: requires sorting and will copy (not inherit) imprint section
 *
 *              <name>
 *
 * `--text=4`   Selected and sorted signatures that are written to the output database
 *              NOTE: requires sorting and will copy (not inherit) imprint section
 *
 *              <mid> <sid> <tid> <name> <Qmid> <Tmid> <Fmid> <HeadMids> <Safe/Nonsafe-member> <Safe/Nonsafe-signature>
 *
 * `--text=5`	Output is sql for advanced analysis
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <jansson.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include "config.h"
#include "database.h"
#include "dbtool.h"
#include "generator.h"
#include "metrics.h"
#include "restartdata.h"
#include "tinytree.h"

/**
 * @date 2020-03-14 11:10:15
 *
 * Main program logic as application context
 * It is contained as an independent `struct` so it can be easily included into projects/code
 *
 * @typedef {object}
 */
struct genmemberContext_t : dbtool_t {

	enum {
		/// @constant {number} - `--text` modes
		OPTTEXT_WON     = 1,
		OPTTEXT_COMPARE = 2,
		OPTTEXT_BRIEF   = 3,
		OPTTEXT_VERBOSE = 4,
		OPTTEXT_SQL     = 5,
	};

	/*
	 * User specified program arguments and options
	 */

	/// @var {string} name of input database
	const char *arg_inputDatabase;
	/// @var {number} Tree size in nodes to be generated for this invocation;
	unsigned   arg_numNodes;
	/// @var {string} name of output database
	const char *arg_outputDatabase;
	/// @var {number} --force, force overwriting of database if already exists
	unsigned   opt_force;
	/// @var {number} Invoke generator for new candidates
	unsigned   opt_generate;
	/// @var {string} name of file containing members
	const char *opt_load;
	/// @var {number} save level-1 indices (hintIndex, signatureIndex, ImprintIndex) and level-2 index (imprints)
	unsigned   opt_saveIndex;
	/// @var {number} --score, group by `tinyTree_t::calcScoreName()` instead of node count, for smaller signature groups
	unsigned   opt_score;
	/// @var {number} Sid range upper bound
	unsigned   opt_sidHi;
	/// @var {number} Sid range lower bound
	unsigned   opt_sidLo;
	/// @var {number} task Id. First task=1
	unsigned   opt_taskId;
	/// @var {number} Number of tasks / last task
	unsigned   opt_taskLast;
	/// @var {number} --text, textual output instead of binary database
	unsigned   opt_text;
	/// @var {number} truncate on database overflow
	double     opt_truncate;
	/// @var {number} generator upper bound
	uint64_t   opt_windowHi;
	/// @var {number} generator lower bound
	uint64_t   opt_windowLo;

	/// @var {footprint_t[]} - Evaluator for forward transforms
	footprint_t *pEvalFwd;
	/// @var {footprint_t[]} - Evaluator for reverse transforms
	footprint_t *pEvalRev;
	/// @var {uint16_t} - score of signature group members. NOTE: size+score may differ from signature
	uint16_t    *pSafeScores;
	/// @var {database_t} - Database store to place results
	database_t  *pStore;

	/// @var {unsigned} - active index for `hints[]`
	unsigned        activeHintIndex;
	/// @var {number} - Head of list of free members to allocate
	unsigned        freeMemberRoot;
	/// @var {number} - THE generator
	generatorTree_t generator;
	/// @var {number} - Number of empty signatures left
	unsigned        numEmpty;
	/// @var {number} - Number of unsafe signatures left
	unsigned        numUnsafe;
	/// @var {number} `foundTree()` duplicate by name
	unsigned        skipDuplicate;
	/// @var {number} `foundTree()` too large for signature
	unsigned        skipSize;
	/// @var {number} `foundTree()` unsafe abundance
	unsigned        skipUnsafe;
	/// @var {number} Where database overflow was caught
	uint64_t        truncated;
	/// @var {number} Name of signature causing overflow
	char            truncatedName[tinyTree_t::TINYTREE_NAMELEN + 1];

	/**
	 * Constructor
	 */
	genmemberContext_t(context_t &ctx) : dbtool_t(ctx), generator(ctx) {
		// arguments and options
		arg_inputDatabase  = NULL;
		arg_numNodes       = 0;
		arg_outputDatabase = NULL;
		opt_force          = 0;
		opt_generate       = 1;
		opt_saveIndex      = 1;
		opt_taskId         = 0;
		opt_taskLast       = 0;
		opt_load           = NULL;
		opt_score          = 0;
		opt_sidHi          = 0;
		opt_sidLo          = 0;
		opt_text           = 0;
		opt_truncate       = 0;
		opt_windowHi       = 0;
		opt_windowLo       = 0;

		pStore      = NULL;
		pEvalFwd    = NULL;
		pEvalRev    = NULL;
		pSafeScores = NULL;

		activeHintIndex  = 0;
		freeMemberRoot   = 0;
		numUnsafe        = 0;
		skipDuplicate    = 0;
		skipSize         = 0;
		skipUnsafe       = 0;
		truncated        = 0;
		truncatedName[0] = 0;
	}

	/**
	 * @date 2020-04-04 22:00:59
	 *
	 * Determine heads and tails and lookup their `memberID` and `signatureId`
	 *
	 * @date 2020-03-29 15:34:32
	 *
	 * Analyse and lookup components (tails)
	 *
	 * Components might have (from a component point of view) a different ordering
	 * like the `F` component in `"ab+bc+a12!!"` which is `"ab+bc+a12!!"`, giving a problem as `"cab+ca+!/bca"`
	 *
	 * Filter them out (by utilizing that `saveString()` does not order)
	 *
	 * example of unsafe components: `"ebcabc?!ad1!!"`
	 *   components are `"a"`, `"bcabc?"` and `"adbcabc?!!"`
	 *   `"adbcabc?!!"` is unsafe because it can be rewritten as `"cdab^!/bcad"`
	 *
	 * @param {member_t} pMember - Member to process
	 * @param {tinyTree_t} treeR - candidate tree
	 * @return {bool} - true for found, false to drop candidate
	 */
	bool /*__attribute__((optimize("O0")))*/ findHeadTail(member_t *pMember, const tinyTree_t &treeR) {

		assert(!(treeR.root & IBIT));

		// safe until proven otherwise
		pMember->flags |= member_t::MEMMASK_SAFE;

		/*
		 * @date 2020-03-29 23:16:43
		 *
		 * Reserved root entries
		 *
		 * `"N[0] = 0?!0:0"` // zero value, zero QnTF operator, zero reference
		 * `"N[a] = 0?!0:a"` // self reference
		 */
		if (treeR.root == 0) {
			assert(::strcmp(pMember->name, "0") == 0); // must be reserved name
			assert(pMember->sid == 1); // must be reserved entry

			pMember->tid = 0;
			pMember->Q = pMember->T = pMember->F = pMember - pStore->members;

			return true;
		}
		if (treeR.root == tinyTree_t::TINYTREE_KSTART) {
			assert(::strcmp(pMember->name, "a") == 0); // must be reserved name
			assert(pMember->sid == 2); // must be reserved entry

			pMember->tid = 0;
			pMember->Q = pMember->T = pMember->F = pMember - pStore->members;
			return true;
		}

		assert(treeR.root >= tinyTree_t::TINYTREE_NSTART);

		/*
		 * @date 2020-03-29 23:36:18
		 *
		 * Extract components and lookup if they exist.
		 * Components need to be validated signature group members.
		 * If no member is found then this candidate will never appear during run-time.
		 *
		 * Don't reject, just flag as unsafe.
		 *
		 * This is because there are single member groups that use unnormalised components.
		 * Example "faedabc?^?2!".
		 *
		 * The 'T' component is "aedabc?^?" which would/should normalise to "aecd^?"
		 * However, this component cannot be rewritten because `F` has a reference lock on the "^".
		 *
		 * Trying to create the tree using the display name will have the effect that `T` will be substituted by "aecd^?" and `F` expanded to "dabc?^"
		 * resulting in "faecd^?dabc?^!" which is one node larger.
		 *
		 * There is a reasonable chance that the result will create a loop during reconstruction.
		 * For that reason the candidate is flagged unsafe.
		 *
		 * For lower-level normalisation these entries could be dropped
		 * but on higher levels ignoring these might cause duplicate/similars to occur resulting in uncontrolled growth of expression trees.
		 *
		 * for 4n9, 2976 of the 791646 signatures are unsafe.
		 */
		tinyTree_t tree(ctx);
		tinyTree_t tree2(ctx);
		char skin[MAXSLOTS + 1];
		char name[tinyTree_t::TINYTREE_NAMELEN + 1];

		{
			unsigned Q = treeR.N[treeR.root].Q;
			{
				// fast
				treeR.saveString(Q, name, skin);
				unsigned ix = pStore->lookupMember(name);
				if (pStore->memberIndex[ix] == 0) {
					// slow
					tree2.loadStringSafe(name);
					tree2.saveString(tree2.root, name, skin);
					ix = pStore->lookupMember(name);
				}

				pMember->Q = pStore->memberIndex[ix];

				// member is unsafe if component not found or unsafe
				if (pMember->Q == 0 || (!(pStore->members[pMember->Q].flags & member_t::MEMMASK_SAFE))) {
					pMember->flags &= ~member_t::MEMMASK_SAFE;
					return false;
				}
			}

			unsigned To = treeR.N[treeR.root].T & ~IBIT;
			{
				// fast
				treeR.saveString(To, name, skin);
				unsigned ix = pStore->lookupMember(name);
				if (pStore->memberIndex[ix] == 0) {
					// slow
					tree2.loadStringSafe(name);
					tree2.saveString(tree2.root, name, skin);
					ix = pStore->lookupMember(name);
				}

				pMember->T = pStore->memberIndex[ix];

				// member is unsafe if component not found or unsafe
				if (pMember->T == 0 || (!(pStore->members[pMember->T].flags & member_t::MEMMASK_SAFE))) {
					pMember->flags &= ~member_t::MEMMASK_SAFE;
					return false;
				}
			}

			unsigned F = treeR.N[treeR.root].F;
			if (F == To) {
				// de-dup T/F
				pMember->F = 0;
			} else {
				// fast
				treeR.saveString(F, name, skin);
				unsigned ix = pStore->lookupMember(name);
				if (pStore->memberIndex[ix] == 0) {
					// slow
					tree2.loadStringSafe(name);
					tree2.saveString(tree2.root, name, skin);
					ix = pStore->lookupMember(name);
				}


				pMember->F = pStore->memberIndex[ix];

				// member is unsafe if component not found or unsafe
				if (pMember->F == 0 || (!(pStore->members[pMember->F].flags & member_t::MEMMASK_SAFE))) {
					pMember->flags &= ~member_t::MEMMASK_SAFE;
					return false;
				}
			}
		}

		/*
		 * @date 2020-04-01 22:30:09
		 *
		 * Analyse and lookup providers (heads)
		 *
		 * example of unsafe head: `"cbdabc!!e21!!"`
		 *   Heads are `"eabc!dc1!!"`, `"cedabc!e!!"` and `"cbdabc!!e!"`
		 *   `"cbdabc!!e!"` is unsafe because that can be rewritten to `"cab&d?/bdce"`
		 */
		{
			unsigned   numHead = 0; // number of unique found heads

			/*
			 * In turn, select each node to become "hot"
			 * Hot nodes are replaced with an endpoint placeholder
			 * Basically cutting of parts of the tree
			 */

			// replace `hot` node with placeholder
			for (unsigned iHead = tinyTree_t::TINYTREE_NSTART; iHead < treeR.root; iHead++) {
				unsigned select                     = 1 << treeR.root | 1 << 0; // selected nodes to extract nodes
				unsigned nextPlaceholderPlaceholder = tinyTree_t::TINYTREE_KSTART;
				uint32_t what[tinyTree_t::TINYTREE_NEND];
				what[0] = 0; // replacement for zero

				// scan tree for needed nodes, ignoring `hot` node
				for (unsigned k = treeR.root; k >= tinyTree_t::TINYTREE_NSTART; k--) {
					if (k != iHead && (select & (1 << k))) {
						const tinyNode_t *pNode = treeR.N + k;
						const unsigned   Q      = pNode->Q;
						const unsigned   To     = pNode->T & ~IBIT;
						const unsigned   F      = pNode->F;

						if (Q >= tinyTree_t::TINYTREE_NSTART)
							select |= 1 << Q;
						if (To >= tinyTree_t::TINYTREE_NSTART)
							select |= 1 << To;
						if (F >= tinyTree_t::TINYTREE_NSTART)
							select |= 1 << F;
					}
				}

				// prepare for extraction
				tree.clearTree();
				// remove `hot` node from selection
				select &= ~(1 << iHead);

				/*
				 * Extract head.
				 * Replacing references by placeholders changes dyadic ordering.
				 * `what[hot]` is not a reference but a placeholder
				 */
				for (unsigned k = tinyTree_t::TINYTREE_NSTART; k <= treeR.root; k++) {
					if (k != iHead && select & (1 << k)) {
						const tinyNode_t *pNode = treeR.N + k;
						const unsigned   Q      = pNode->Q;
						const unsigned   To     = pNode->T & ~IBIT;
						const unsigned   Ti     = pNode->T & IBIT;
						const unsigned   F      = pNode->F;

						// assign placeholder to endpoint or `hot`
						if (!(select & (1 << Q))) {
							what[Q] = nextPlaceholderPlaceholder++;
							select |= 1 << Q;
						}
						if (!(select & (1 << To))) {
							what[To] = nextPlaceholderPlaceholder++;
							select |= 1 << To;
						}
						if (!(select & (1 << F))) {
							what[F] = nextPlaceholderPlaceholder++;
							select |= 1 << F;
						}

						// mark replacement of old node
						what[k] = tree.count;
						select |= 1 << k;

						/*
						 * Reminder:
						 *  [ 2] a ? ~0 : b                  "+" OR
						 *  [ 6] a ? ~b : 0                  ">" GT
						 *  [ 8] a ? ~b : b                  "^" XOR
						 *  [ 9] a ? ~b : c                  "!" QnTF
						 *  [16] a ?  b : 0                  "&" AND
						 *  [19] a ?  b : c                  "?" QTF
						 */

						// perform dyadic ordering
						if (To == 0 && Ti && tree.compare(what[Q], tree, what[F]) > 0) {
							// reorder OR
							tree.N[tree.count].Q = what[F];
							tree.N[tree.count].T = IBIT;
							tree.N[tree.count].F = what[Q];
						} else if (To == F && tree.compare(what[Q], tree, what[F]) > 0) {
							// reorder XOR
							tree.N[tree.count].Q = what[F];
							tree.N[tree.count].T = what[Q] ^ IBIT;
							tree.N[tree.count].F = what[Q];
						} else if (F == 0 && !Ti && tree.compare(what[Q], tree, what[To]) > 0) {
							// reorder AND
							tree.N[tree.count].Q = what[To];
							tree.N[tree.count].T = what[Q];
							tree.N[tree.count].F = 0;
						} else {
							// default
							tree.N[tree.count].Q = what[Q];
							tree.N[tree.count].T = what[To] ^ Ti;
							tree.N[tree.count].F = what[F];
						}

						tree.count++;
					}
				}

				// set root
				tree.root = tree.count - 1;

				/*
				 * @date 2021-06-14 18:56:37
				 *
				 * This doesn't got well for sid=221 "dab+c1&!"
				 * When replacing "ab+" with a placeholder the result is "dxcx&!"
				 * making the head effectively "cbab&!" instead of "caab&!".
				 * this also introduces a layer of transforms.
				 *
				 * This makes it clear that heads should not be used for structure creation
				 * and therefore be a sid/tid combo instead of a references to a template member
				 * This sadly adds 5-6 entryes to `member_t`.
				 *
				 * This change should be safe because the components have already been tested for validity.
				 */

				// fast path: lookup skin-free head name/notation
				tree.saveString(tree.root, name, skin);
				unsigned ix = pStore->lookupMember(name);
				if (pStore->memberIndex[ix] == 0) {
					/*
					 * @date 2021-06-18 21:29:50
					 *
					 * NOTE/WARNING the extracted component may have non-normalised dyadic ordering
					 * because in the context of the original trees, the endpoints were locked by the now removed node
					 */
					tree2.loadStringSafe(name);
					// structure is now okay
					tree2.saveString(tree2.root, name, skin);
					// endpoints are now okay

					ix = pStore->lookupMember(name);
				}
				unsigned midHead = pStore->memberIndex[ix];

				if (midHead == 0) {
					// component not found
					pMember->flags &= ~member_t::MEMMASK_SAFE;
					return false;
				}

				if (!(pStore->members[midHead].flags & member_t::MEMMASK_SAFE)) {
					// component unsafe
					pMember->flags &= ~member_t::MEMMASK_SAFE;
					return false;
				}

				// test if head already present
				for (unsigned k = 0; k < member_t::MAXHEAD && pMember->heads[k]; k++) {
					if (pMember->heads[k] == midHead) {
						// found
						midHead = 0;
						break;
					}
				}

				// add to list
				if (midHead) {
					assert(numHead < member_t::MAXHEAD);
					pMember->heads[numHead++] = midHead;
				}
			}
		}

		return true;
	}

	/**
	 * @date 2020-04-08 16:01:14
	 *
	 * Allocate a new member, either by popping free list or assigning new
	 * Member if zero except for name
	 *
	 * @param {string} pName - name/notation of member
	 * @return {member_t}
	 */
	member_t *memberAlloc(const char *pName) {
		member_t *pMember;

		unsigned mid = freeMemberRoot;
		if (mid) {
			pMember        = pStore->members + mid;
			freeMemberRoot = pMember->nextMember; // pop from free list
			::strcpy(pMember->name, pName); // populate with name
		} else {
			mid     = pStore->addMember(pName); // allocate new member
			pMember = pStore->members + mid;
		}

		return pMember;
	}

	/**
	 * @date 2020-04-08 16:04:15
	 *
	 * Release member by pushing it on the free list
	 *
	 * @param pMember
	 */
	void memberFree(member_t *pMember) {
		// zero orphan so it won't be found by `lookupMember()`
		::memset(pMember, 0, sizeof(*pMember));

		// push member on the freelist
		pMember->nextMember = freeMemberRoot;
		freeMemberRoot = pMember - pStore->members;
	}

	/**
	 * @date 2020-03-28 18:29:25
	 *
	 * Test if candidate can be a signature group member and add when possible
	 *
	 * @date 2020-04-02 11:41:44
	 *
	 * for `signature_t`, only use `flags`, `size` and `firstMember`.
	 *
	 * @date 2020-04-15 11:02:46
	 *
	 * For now, collect members only based on size instead of `compareMember()`.
	 * Member properties still need to be discovered to make strategic decisions.
	 * Collecting members is too expensive to ask questions on missing members later.
	 *
	 * @param {generatorTree_t} treeR - candidate tree
	 * @param {string} pNameR - Tree name/notation
	 * @param {number} numPlaceholder - number of unique endpoints/placeholders in tree
	 * @param {number} numEndpoint - number of non-zero endpoints in tree
	 * @param {number} numBackRef - number of back-references
	 * @return {boolean} return `true` to continue with recursion (this should be always the case except for `genrestartdata`)
	 */
	bool /*__attribute__((optimize("O0")))*/ foundTreeMember(const generatorTree_t &treeR, const char *pNameR, unsigned numPlaceholder, unsigned numEndpoint, unsigned numBackRef) {

		if (this->truncated)
			return false; // quit as fast as possible

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK && ctx.tick) {
			int perSecond = ctx.updateSpeed();

			if (perSecond == 0 || ctx.progress > ctx.progressHi) {
				fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) | numMember=%u(%.0f%%) numEmpty=%u numUnsafe=%u | skipDuplicate=%u skipSize=%u skipUnsafe=%u | hash=%.3f",
					ctx.timeAsString(), ctx.progress, perSecond,
					pStore->numMember, pStore->numMember * 100.0 / pStore->maxMember,
					numEmpty, numUnsafe - numEmpty,
					skipDuplicate, skipSize, skipUnsafe, (double) ctx.cntCompare / ctx.cntHash);
			} else {
				int eta = (int) ((ctx.progressHi - ctx.progress) / perSecond);

				int etaH = eta / 3600;
				eta %= 3600;
				int etaM = eta / 60;
				eta %= 60;
				int etaS = eta;

				fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) %.5f%% eta=%d:%02d:%02d | numMember=%u(%.0f%%) numEmpty=%u numUnsafe=%u | skipDuplicate=%u skipSize=%u skipUnsafe=%u | hash=%.3f %s",
					ctx.timeAsString(), ctx.progress, perSecond, (ctx.progress - treeR.windowLo) * 100.0 / (ctx.progressHi - treeR.windowLo), etaH, etaM, etaS,
					pStore->numMember, pStore->numMember * 100.0 / pStore->maxMember,
					numEmpty, numUnsafe - numEmpty,
					skipDuplicate, skipSize, skipUnsafe, (double) ctx.cntCompare / ctx.cntHash, pNameR);
			}

			if (ctx.restartTick) {
				// passed a restart point
				fprintf(stderr, "\n");
				ctx.restartTick = 0;
			}

			ctx.tick = 0;
		}

		/*
		 * test  for duplicates
		 */

		unsigned mix = pStore->lookupMember(pNameR);
		if (pStore->memberIndex[mix] != 0) {
			// duplicate candidate name
			skipDuplicate++;
			return true;
		}

		/*
		 * Test for database overflow
		 */
		if (this->opt_truncate) {
			// avoid `"storage full"`. Give warning later
			if (pStore->maxImprint - pStore->numImprint <= pStore->interleave || pStore->maxSignature - pStore->numSignature <= 1) {
				// break now, display text later/ Leave progress untouched
				this->truncated = ctx.progress;
				::strcpy(this->truncatedName, pNameR);

				// quit as fast as possible
				return false;
			}
		}

		/*
		 * Find the matching signature group. It's layout only so ignore transformId.
		 */

		unsigned sid     = 0;
		unsigned tid     = 0;
		unsigned markSid = pStore->numSignature;

		if ((ctx.flags & context_t::MAGICMASK_AINF) && !this->readOnlyMode) {
			/*
			 * @date 2020-04-25 22:00:29
			 *
			 * WARNING: add-if-not-found only checks tid=0 to determine if (not-)found.
			 *          This creates false-positives.
			 *          Great for high-speed loading, but not for perfect duplicate detection.
			 *          To get better results, re-run with next increment interleave.
			 */
			// add to imprints to index
			sid = pStore->addImprintAssociative(&treeR, pEvalFwd, pEvalRev, markSid);
		} else {
			pStore->lookupImprintAssociative(&treeR, pEvalFwd, pEvalRev, &sid, &tid);
		}

		if (sid == 0)
			return true; // not found

		signature_t *pSignature = pStore->signatures + sid;
		unsigned cmp = 0;
		unsigned scoreR = 0; // delay calculation of this as long as possible

		/*
		 * early-reject
		 *
		 * @date 2021-06-21 08:59:05
		 * In `--text=SQL`, collect as many members in a group as possible so they can be examined/selected/rejected on further analysis
		 */

		if ((pSignature->flags & signature_t::SIGMASK_SAFE) && opt_text != OPTTEXT_SQL) {
			/*
			 * @date 2021-06-20 19:06:44
			 * Just like primes with component dependency chains, members can be larger than signatures
			 * Larger candidates will always be rejected, so reject now before doing expensive testing
			 * Grouping can be either by node size or score
			 *
			 * NOTE:
			 * `pSafeScores[]` contains either score (opt_score!=0) or node count (opt_score==0)
			 */

			if (opt_score) {
				if (scoreR == 0)
					scoreR = treeR.calcScoreName(pNameR);

				if (scoreR > pSafeScores[sid])
					cmp = '*'; // reject
			} else if (treeR.count - tinyTree_t::TINYTREE_NSTART > pSafeScores[sid]) {
				cmp = '*'; // reject
			}
		} else {
			/*
			 * @date 2021-06-20 19:15:49
			 * unsafe groups are a collection of everything that matches.
			 * however, keep the difference less than 2 nodes, primarily to protect 5n9 against populating <= 3n9
			 */
			if (treeR.count - tinyTree_t::TINYTREE_NSTART > pSignature->size + 1u)
				cmp = '*'; // reject
		}

		if (cmp) {
			if (opt_text == OPTTEXT_COMPARE)
				printf("%lu\t%u\t%c\t%s\t%u\t%u\t%u\t%u\n", ctx.progress, sid, cmp, pNameR, treeR.count - tinyTree_t::TINYTREE_NSTART, numPlaceholder, numEndpoint, numBackRef);
			skipSize++;
			return true;
		}

		/*
		 * Determine if safe when heads/tails are all safe
		 * NOTE: need temporary storage because database member section might be readOnly
		 */

		member_t tmpMember;
		::memset(&tmpMember, 0, sizeof(tmpMember));

		::strcpy(tmpMember.name, pNameR);
		tmpMember.sid            = sid;
		tmpMember.tid            = tid;
		tmpMember.size           = treeR.count - tinyTree_t::TINYTREE_NSTART;
		tmpMember.numPlaceholder = numPlaceholder;
		tmpMember.numEndpoint    = numEndpoint;
		tmpMember.numBackRef     = numBackRef;


		bool found = findHeadTail(&tmpMember, treeR);
		if (!found)
			found = true; // for debugging

		/*
		 * Verify if candidate member is acceptable
		 */

		if (pSignature->flags & signature_t::SIGMASK_SAFE) {
			if (!(tmpMember.flags & member_t::MEMMASK_SAFE)) {
				// group is safe, candidate not. Reject
				cmp = '<';
				skipUnsafe++;
			} else {
				// group/candidate both safe. Accept
				cmp = '+';

				if (opt_score) {
					if (scoreR == 0)
						scoreR = treeR.calcScoreName(pNameR);
					if (scoreR < pSafeScores[sid])
						cmp = '!'; // better score, flush group
				}
			}
		} else {
			if (tmpMember.flags & member_t::MEMMASK_SAFE) {
				// group is unsafe, candidate is safe. Accept
				cmp = '>';
			} else {
				// group/candidate both unsafe. Accept.
				cmp = '=';
			}
		}

		if (opt_text == OPTTEXT_COMPARE)
			printf("%lu\t%u\t%c\t%s\t%u\t%u\t%u\t%u\n", ctx.progress, tmpMember.sid, cmp, tmpMember.name, tmpMember.size, tmpMember.numPlaceholder, tmpMember.numEndpoint, tmpMember.numBackRef);

		if (cmp == '<' || cmp == '-')
			return true;  // lost challenge

		// won challenge
		if (opt_text == OPTTEXT_WON)
			printf("%s\n", pNameR);

		if (cmp == '>' || cmp == '!') {
			/*
			 * group changes from unsafe to save, or safe group flush: remove all (unsafe) members
			 */

			if (pSignature->firstMember) {
				// remove all unsafe members

				if (this->readOnlyMode) {
					// member chain cannot be modified
					// pretend signature becomes safe or keeps unsafe members
					pSignature->firstMember = 0;
				} else {
					/*
					 * Group contains unsafe members of same size.
					 * empty group
					 *
					 * @date 2020-04-05 02:21:42
					 *
					 * For `5n9-pure` it turns out that the chance of finding safe replacements is rare.
					 * And you need to collect all non-safe members if the group is unsafe.
					 * Orphaning them depletes resources too fast.
					 *
					 * Reuse `members[]`.
					 * Field `nextMember` is perfect for that.
					 */
					while (pSignature->firstMember) {
						// remove all references to the deleted
						for (unsigned iMid = 1; iMid < pStore->numMember; iMid++) {
							member_t *p = pStore->members + iMid;

							if (p->Q == pSignature->firstMember) {
								assert(!(p->flags & member_t::MEMMASK_SAFE));
								p->Q = 0;
							}
							if (p->T == pSignature->firstMember) {
								assert(!(p->flags & member_t::MEMMASK_SAFE));
								p->T = 0;
							}
							if (p->F == pSignature->firstMember) {
								assert(!(p->flags & member_t::MEMMASK_SAFE));
								p->F = 0;
							}
						}

						// release head of chain
						member_t *p = pStore->members + pSignature->firstMember;

						pSignature->firstMember = p->nextMember;

						this->memberFree(p);
					}
				}

				// group has become empty
				numEmpty++;
			}
		}

		if (cmp == '>') {
			// mark group as safe
			pSignature->flags |= signature_t::SIGMASK_SAFE;
			numUnsafe--;
		}

		// going to add member to group.
		if (pSignature->firstMember == 0)
			numEmpty--;

		/*
		 * promote candidate to member
		 */

		if (this->readOnlyMode != 0) {
			// link a fake member to mark non-empty
			pSignature->firstMember = 1;
		} else {
			// allocate
			member_t *pMember = this->memberAlloc(pNameR);

			// populate
			*pMember = tmpMember;

			// link
			pMember->nextMember     = pSignature->firstMember;
			pSignature->firstMember = pMember - pStore->members;

			// index
			pStore->memberIndex[mix] = pMember - pStore->members;
		}

		/*
		 * update global score
		 * NOTE: `pSafeScores[]` contains either score (opt_score!=0) or node count (opt_score==0)
		 */

		if (opt_score) {
			if (scoreR == 0)
				scoreR = treeR.calcScoreName(pNameR);
			pSafeScores[sid] = scoreR;
		} else {
			pSafeScores[sid] = treeR.count - tinyTree_t::TINYTREE_NSTART;
		}

		return true;
	}

	/**
	 * @date 2020-04-05 21:07:14
	 *
	 * Compare function for `qsort_r`
	 *
	 * @param {member_t} lhs - left hand side member
	 * @param {member_t} rhs - right hand side member
	 * @param {context_t} arg - I/O context
	 * @return "<0" if "L<R", "0" if "L==R", ">0" if "L>R"
	 */
	static int comparMember(const void *lhs, const void *rhs, void *arg) {
		if (lhs == rhs)
			return 0;

		const member_t *pMemberL = static_cast<const member_t *>(lhs);
		const member_t *pMemberR = static_cast<const member_t *>(rhs);
		context_t      *pApp     = static_cast<context_t *>(arg);

		// test for empties (they should gather towards the end of `members[]`)
		if (pMemberL->sid == 0 && pMemberR->sid == 0)
			return 0;
		if (pMemberL->sid == 0)
			return +1;
		if (pMemberR->sid == 0)
			return -1;

		int cmp = 0;

		/*
		 * compare scores
		 */

		unsigned scoreL = tinyTree_t::calcScoreName(pMemberL->name);
		unsigned scoreR = tinyTree_t::calcScoreName(pMemberR->name);

		cmp = scoreL - scoreR;
		if (cmp)
			return cmp;

		/*
		 * Compare trees
		 */

		// load trees
		tinyTree_t treeL(*pApp);
		tinyTree_t treeR(*pApp);

		treeL.loadStringFast(pMemberL->name);
		treeR.loadStringFast(pMemberR->name);

		cmp = treeL.compare(treeL.root, treeR, treeR.root);
		return cmp;
	}

	/**
	 * @date 2020-04-02 21:52:34
	 */
	void rebuildImprints(unsigned unsafeOnly) {
		// clear signature and imprint index
		::memset(pStore->imprintIndex, 0, pStore->imprintIndexSize * sizeof(*pStore->imprintIndex));

		if (pStore->numSignature < 2)
			return; //nothing to do

		// skip reserved entry
		pStore->numImprint = 1;

		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS) {
			if (unsafeOnly)
				fprintf(stderr, "[%s] Rebuilding imprints for empty/unsafe signatures\n", ctx.timeAsString());
			else
				fprintf(stderr, "[%s] Rebuilding imprints\n", ctx.timeAsString());
		}

		/*
		 * Create imprints for signature groups
		 */

		generatorTree_t tree(ctx);

		// show window
		if (opt_sidLo || opt_sidHi) {
			if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
				fprintf(stderr, "[%s] Sid window: %u-%u\n", ctx.timeAsString(), opt_sidLo, opt_sidHi ? opt_sidHi : pStore->numSignature);
		}

		// reset ticker
		ctx.setupSpeed(pStore->numSignature);
		ctx.tick = 0;

		// re-calculate
		numEmpty = numUnsafe = 0;

		// create imprints for signature groups
		ctx.progress++; // skip reserved
		for (unsigned iSid = 1; iSid < pStore->numSignature; iSid++) {
			if (ctx.opt_verbose >= ctx.VERBOSE_TICK && ctx.tick) {
				int perSecond = ctx.updateSpeed();

				if (perSecond == 0 || ctx.progress > ctx.progressHi) {
					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) | numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond,
						pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
						numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);
				} else {
					int eta = (int) ((ctx.progressHi - ctx.progress) / perSecond);

					int etaH = eta / 3600;
					eta %= 3600;
					int etaM = eta / 60;
					eta %= 60;
					int etaS = eta;

					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) %.5f%% eta=%d:%02d:%02d | numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond, ctx.progress * 100.0 / ctx.progressHi, etaH, etaM, etaS,
						pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
						numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);
				}

				ctx.tick = 0;
			}

			if ((opt_sidLo && iSid < opt_sidLo) || (opt_sidHi && iSid >= opt_sidHi)) {
				ctx.progress++;
				continue;
			}

			const signature_t *pSignature = pStore->signatures + iSid;

			/*
			 * Add to imprint index, either all or empty/unsafe only
			 */

			if (!unsafeOnly || !(pSignature->flags & signature_t::SIGMASK_SAFE)) {
				// avoid `"storage full"`. Give warning later
				if (pStore->maxImprint - pStore->numImprint <= pStore->interleave && opt_sidHi == 0 && this->opt_truncate) {
					// break now, display text later/ Leave progress untouched
					assert(iSid == ctx.progress);
					break;
				}

				tree.loadStringFast(pSignature->name);

				unsigned sid, tid;

				if (!pStore->lookupImprintAssociative(&tree, pEvalFwd, pEvalRev, &sid, &tid))
					pStore->addImprintAssociative(&tree, this->pEvalFwd, this->pEvalRev, iSid);
			}

			// stats
			if (pSignature->firstMember == 0)
				numEmpty++;
			if (!(pSignature->flags & signature_t::SIGMASK_SAFE))
				numUnsafe++;

			ctx.progress++;
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");

		if (ctx.progress != ctx.progressHi && opt_sidHi == 0) {
			fprintf(stderr, "[%s] WARNING: Imprint storage full. Truncating at sid=%u \"%s\"\n",
				ctx.timeAsString(), (unsigned) ctx.progress, pStore->signatures[ctx.progress].name);
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] Created imprints. numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f\n",
				ctx.timeAsString(),
				pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
				numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);
	}

	/**
	 * @date 2020-04-20 19:57:08
	 *
	 * Compare function for `qsort_r`
	 *
	 * Compare two hints.
	 * Do not compare them directly, but use the arguments as index to `database_t::hints[]`.
	 *
	 * @param {signature_t} lhs - left hand side hint index
	 * @param {signature_t} rhs - right hand side hint index
	 * @param {genhintContext_t} arg - Application context
	 * @return "<0" if "L<R", "0" if "L==R", ">0" if "L>R"
	 */
	static int comparHint(const void *lhs, const void *rhs, void *arg) {
		if (lhs == rhs)
			return 0;

		genmemberContext_t *pApp        = static_cast<genmemberContext_t *>(arg);
		// Arguments are signature offsets
		const signature_t  *pSignatureL = pApp->pStore->signatures + *(unsigned *) lhs;
		const signature_t  *pSignatureR = pApp->pStore->signatures + *(unsigned *) rhs;
		const hint_t       *pHintL      = pApp->pStore->hints + pSignatureL->hintId;
		const hint_t       *pHintR      = pApp->pStore->hints + pSignatureR->hintId;

		int cmp;

		// first compare active index (lowest first)
		cmp = pHintL->numStored[pApp->activeHintIndex] - pHintR->numStored[pApp->activeHintIndex];
		if (cmp)
			return cmp;

		// then compare inactive indices (highest first)
		for (unsigned j = 0; j < hint_t::MAXENTRY; j++) {
			if (j != pApp->activeHintIndex) {
				cmp = pHintR->numStored[j] - pHintL->numStored[j];
				if (cmp)
					return cmp;
			}
		}

		// identical
		return 0;
	}

	/**
	 * @date 2020-05-02 18:40:01
	 */
	void rebuildImprintsWithHints(void) {
		assert(pStore->numHint >= 2);

		// clear signature and imprint index
		::memset(pStore->imprintIndex, 0, pStore->imprintIndexSize * sizeof(*pStore->imprintIndex));

		if (pStore->numSignature < 2)
			return; //nothing to do

		// skip reserved entry
		pStore->numImprint = 1;

		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS) {
			fprintf(stderr, "[%s] Rebuilding imprints with hints\n", ctx.timeAsString());
		}

		/*
		 * Create ordered vector to hints
		 */

		unsigned *pHintMap = (unsigned *) ctx.myAlloc("pHintMap", pStore->maxSignature, sizeof(*pHintMap));

		// locate which hint index
		this->activeHintIndex = 0;
		for (const metricsInterleave_t *pInterleave = metricsInterleave; pInterleave->numSlot; pInterleave++) {
			if (pInterleave->numStored == pStore->interleave) {
				this->activeHintIndex = pInterleave - metricsInterleave;
				break;
			}
		}

		// fill map with offsets to signatures
		unsigned      numHint = 0;
		for (unsigned iSid    = 1; iSid < pStore->numSignature; iSid++) {
			const signature_t *pSignature = pStore->signatures + iSid;

			if (!(pSignature->flags & signature_t::SIGMASK_SAFE))
				pHintMap[numHint++] = iSid;

		}

		// sort entries.
		qsort_r(pHintMap, numHint, sizeof(*pHintMap), comparHint, this);

		/*
		 * Create imprints for signature groups
		 */

		generatorTree_t tree(ctx);

		// reset ticker
		ctx.setupSpeed(numHint);
		ctx.tick = 0;

		// re-calculate
		numEmpty = numUnsafe = 0;

		// create imprints for signature groups
		for (unsigned iHint = 0; iHint < numHint; iHint++) {
			if (ctx.opt_verbose >= ctx.VERBOSE_TICK && ctx.tick) {
				int perSecond = ctx.updateSpeed();

				if (perSecond == 0 || ctx.progress > ctx.progressHi) {
					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) | numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond,
						pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
						numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);
				} else {
					int eta = (int) ((ctx.progressHi - ctx.progress) / perSecond);

					int etaH = eta / 3600;
					eta %= 3600;
					int etaM = eta / 60;
					eta %= 60;
					int etaS = eta;

					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) %.5f%% eta=%d:%02d:%02d | numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond, ctx.progress * 100.0 / ctx.progressHi, etaH, etaM, etaS,
						pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
						numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);
				}

				ctx.tick = 0;
			}

			// get signature
			unsigned          iSid        = pHintMap[iHint];
			const signature_t *pSignature = pStore->signatures + iSid;

			/*
			 * Add to imprint index, either all or empty/unsafe only
			 */

			if (!(pSignature->flags & signature_t::SIGMASK_SAFE)) {
				// avoid `"storage full"`. Give warning later
				if (pStore->maxImprint - pStore->numImprint <= pStore->interleave && opt_sidHi == 0) {
					// break now, display text later/ Leave progress untouched
					assert(iHint == ctx.progress);
					break;
				}

				tree.loadStringFast(pSignature->name);

				unsigned sid = 0, tid;

				if (!pStore->lookupImprintAssociative(&tree, pEvalFwd, pEvalRev, &sid, &tid))
					pStore->addImprintAssociative(&tree, this->pEvalFwd, this->pEvalRev, iSid);
			}

			// stats
			if (pSignature->firstMember == 0)
				numEmpty++;
			if (!(pSignature->flags & signature_t::SIGMASK_SAFE))
				numUnsafe++;

			ctx.progress++;
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");

		if (ctx.progress != ctx.progressHi && opt_sidHi == 0) {
			fprintf(stderr, "[%s] WARNING: Imprint storage full. Truncating at %u \"%s\"\n",
				ctx.timeAsString(), (unsigned) ctx.progress, pStore->signatures[ctx.progress].name);
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] Created imprints. numImprint=%u(%.0f%%) numEmpty=%u numUnsafe=%u | hash=%.3f\n",
				ctx.timeAsString(),
				pStore->numImprint, pStore->numImprint * 100.0 / pStore->maxImprint,
				numEmpty, numUnsafe - numEmpty, (double) ctx.cntCompare / ctx.cntHash);

		ctx.myFree("pSignatureIndex", pHintMap);
	}

	/**
	 * @date 2020-03-22 01:00:05
	 *
	 * Main entrypoint
	 *
	 * Create generator for given dataset and add newly unique signatures to the database
	 */
	void /*__attribute__((optimize("O0")))*/ membersFromFile(void) {

		/*
		 * Load candidates from file.
		 */

		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS)
			fprintf(stderr, "[%s] Reading members from file\n", ctx.timeAsString());

		FILE *f = fopen(this->opt_load, "r");
		if (f == NULL)
			ctx.fatal("\n{\"error\":\"fopen('%s') failed\",\"where\":\"%s:%s:%d\",\"return\":\"%m\"}\n",
				  this->opt_load, __FUNCTION__, __FILE__, __LINE__);

		// apply settings for `--window`
		generator.windowLo = this->opt_windowLo;
		generator.windowHi = this->opt_windowHi;

		// reset ticker
		ctx.setupSpeed(0);
		ctx.tick = 0;
		skipDuplicate = skipSize = skipUnsafe = 0;

		char     name[64];
		unsigned numPlaceholder, numEndpoint, numBackRef;
		this->truncated = 0;

		// <name> [ <numPlaceholder> <numEndpoint> <numBackRef> ]
		for (;;) {
			static char line[512];

			if (::fgets(line, sizeof(line), f) == 0)
				break; // end-of-input

			name[0] = 0;
			int ret = ::sscanf(line, "%s %u %u %u\n", name, &numPlaceholder, &numEndpoint, &numBackRef);

			// calculate values
			unsigned        newPlaceholder = 0, newEndpoint = 0, newBackRef = 0;
			unsigned        beenThere      = 0;
			for (const char *p             = name; *p; p++) {
				if (::islower(*p)) {
					if (!(beenThere & (1 << (*p - 'a')))) {
						newPlaceholder++;
						beenThere |= 1 << (*p - 'a');
					}
					newEndpoint++;
				} else if (::isdigit(*p) && *p != '0') {
					newBackRef++;
				}
			}

			if (ret != 1 && ret != 4)
				ctx.fatal("\n{\"error\":\"bad/empty line\",\"where\":\"%s:%s:%d\",\"linenr\":%lu}\n",
					  __FUNCTION__, __FILE__, __LINE__, ctx.progress);
			if (ret == 4 && (numPlaceholder != newPlaceholder || numEndpoint != newEndpoint || numBackRef != newBackRef))
				ctx.fatal("\n{\"error\":\"line has incorrect values\",\"where\":\"%s:%s:%d\",\"linenr\":%lu}\n",
					  __FUNCTION__, __FILE__, __LINE__, ctx.progress);

			// test if line is within progress range
			// NOTE: first line has `progress==0`
			if ((generator.windowLo && ctx.progress < generator.windowLo) || (generator.windowHi && ctx.progress >= generator.windowHi)) {
				ctx.progress++;
				continue;
			}

			/*
			 * construct tree
			 */
			generator.loadStringFast(name);

			/*
			 * call `foundTreeMember()`
			 */

			if (!foundTreeMember(generator, name, newPlaceholder, newEndpoint, newBackRef))
				break;

			ctx.progress++;
		}

		fclose(f);

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");

		if (truncated) {
			if (ctx.opt_verbose >= ctx.VERBOSE_WARNING)
				fprintf(stderr, "[%s] WARNING: Signature/Imprint storage full. Truncating at progress=%lu \"%s\"\n",
					ctx.timeAsString(), this->truncated, this->truncatedName);

			// save position for final status
			this->opt_windowHi = this->truncated;
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "[%s] Read %lu members. numSignature=%u(%.0f%%) numMember=%u(%.0f%%) numEmpty=%u numUnsafe=%u | skipDuplicate=%u skipSize=%u skipUnsafe=%u\n",
				ctx.timeAsString(),
				ctx.progress,
				pStore->numSignature, pStore->numSignature * 100.0 / pStore->maxSignature,
				pStore->numMember, pStore->numMember * 100.0 / pStore->maxMember,
				numEmpty, numUnsafe - numEmpty,
				skipDuplicate, skipSize, skipUnsafe);
	}

	/**
	 * @date 2020-03-22 01:00:05
	 *
	 * Main entrypoint
	 *
	 * Create generator for given dataset and add newly unique signatures to the database
	 *
	 * @param {database_t} pStore - memory based database
	 */
	void /*__attribute__((optimize("O0")))*/ membersFromGenerator(void) {

		/*
		 * Apply window/task setting on generator
		 */

		if (ctx.opt_verbose >= ctx.VERBOSE_WARNING) {
			if (this->opt_taskId || this->opt_taskLast) {
				if (this->opt_windowHi)
					fprintf(stderr, "[%s] INFO: task=%u,%u window=%lu-%lu\n", ctx.timeAsString(), this->opt_taskId, this->opt_taskLast, this->opt_windowLo, this->opt_windowHi);
				else
					fprintf(stderr, "[%s] INFO: task=%u,%u window=%lu-last\n", ctx.timeAsString(), this->opt_taskId, this->opt_taskLast, this->opt_windowLo);
			} else if (this->opt_windowLo || this->opt_windowHi) {
				if (this->opt_windowHi)
					fprintf(stderr, "[%s] INFO: window=%lu-%lu\n", ctx.timeAsString(), this->opt_windowLo, this->opt_windowHi);
				else
					fprintf(stderr, "[%s] INFO: window=%lu-last\n", ctx.timeAsString(), this->opt_windowLo);
			}
		}

		// apply settings for `--window`
		generator.windowLo = this->opt_windowLo;
		generator.windowHi = this->opt_windowHi;

		// apply restart data for > `4n9`
		unsigned ofs = 0;
		if (this->arg_numNodes > 4 && this->arg_numNodes < tinyTree_t::TINYTREE_MAXNODES)
			ofs = restartIndex[this->arg_numNodes][(ctx.flags & context_t::MAGICMASK_PURE) ? 1 : 0];
		if (ofs)
			generator.pRestartData = restartData + ofs;

		// reset progress
		if (generator.windowHi) {
			ctx.setupSpeed(generator.windowHi);
		} else {
			const metricsGenerator_t *pMetrics = getMetricsGenerator(MAXSLOTS, ctx.flags & context_t::MAGICMASK_PURE, arg_numNodes);
			ctx.setupSpeed(pMetrics ? pMetrics->numProgress : 0);
		}
		ctx.tick = 0;
		skipDuplicate = skipSize = skipUnsafe = 0;

		/*
		 * Generate candidates
		 */
		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS)
			fprintf(stderr, "[%s] Generating candidates for %un%u%s\n", ctx.timeAsString(), arg_numNodes, MAXSLOTS, ctx.flags & context_t::MAGICMASK_PURE ? "-pure" : "");

		if (arg_numNodes == 0) {
			generator.root = 0; // "0"
			foundTreeMember(generator, "0", 0, 0, 0);
			generator.root = 1; // "a"
			foundTreeMember(generator, "a", 1, 1, 0);
		} else {
			unsigned endpointsLeft = arg_numNodes * 2 + 1;

			generator.initialiseGenerator(ctx.flags & context_t::MAGICMASK_PURE);
			generator.clearGenerator();
			generator.generateTrees(arg_numNodes, endpointsLeft, 0, 0, this, static_cast<generatorTree_t::generateTreeCallback_t>(&genmemberContext_t::foundTreeMember));
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");

		if (ctx.progress != ctx.progressHi && this->opt_windowLo == 0 && this->opt_windowHi == 0) {
			// can only test if windowing is disabled
			printf("{\"error\":\"progressHi failed\",\"where\":\"%s:%s:%d\",\"encountered\":%lu,\"expected\":%lu,\"numNode\":%u}\n",
			       __FUNCTION__, __FILE__, __LINE__, ctx.progress, ctx.progressHi, arg_numNodes);
		}

		if (truncated) {
			if (ctx.opt_verbose >= ctx.VERBOSE_WARNING)
				fprintf(stderr, "[%s] WARNING: Signature/Imprint storage full. Truncating at progress=%lu \"%s\"\n",
					ctx.timeAsString(), this->truncated, this->truncatedName);
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] numSlot=%u pure=%u numNode=%u numCandidate=%lu numMember=%u(%.0f%%) numEmpty=%u numUnsafe=%u | skipDuplicate=%u skipSize=%u skipUnsafe=%u\n",
				ctx.timeAsString(), MAXSLOTS, (ctx.flags & context_t::MAGICMASK_PURE) ? 1 : 0, arg_numNodes, ctx.progress,
				pStore->numMember, pStore->numMember * 100.0 / pStore->maxMember,
				numEmpty, numUnsafe - numEmpty,
				skipDuplicate, skipSize, skipUnsafe);
	}

	/**
	 * @date 2020-04-07 22:53:08
	 *
	 * Rebuild members by compacting them (removing orphans), sorting and re-chaining them.
	 *
	 * This should have no effect pre-loaded members (they were already sorted)
	 *
	 * Groups may contain (unsafe) members that got orphaned when accepting a safe member.
	 */
	void /*__attribute__((optimize("O0")))*/ finaliseMembers(void) {
		tinyTree_t tree(ctx);

		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS)
			fprintf(stderr, "[%s] Sorting members\n", ctx.timeAsString());

		unsigned lastMember = pStore->numMember;

		// clear member index and linked-list
		::memset(pStore->memberIndex, 0, pStore->memberIndexSize * sizeof(*pStore->memberIndex));
		for (unsigned iSid = 0; iSid < pStore->numSignature; iSid++)
			pStore->signatures[iSid].firstMember = 0;
		pStore->numMember                            = 1;
		skipDuplicate = skipSize = skipUnsafe = 0;

		// sort entries (skipping first)
		assert(lastMember >= 1);
		qsort_r(pStore->members + 1, lastMember - 1, sizeof(*pStore->members), comparMember, this);

		if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS)
			fprintf(stderr, "[%s] Indexing members\n", ctx.timeAsString());

		// reload everything
		ctx.setupSpeed(lastMember);
		ctx.tick = 0;

		// mark signatures unsafe
		for (unsigned iSid = 0; iSid < pStore->numSignature; iSid++)
			pStore->signatures[iSid].flags &= ~signature_t::SIGMASK_SAFE;

		ctx.progress++; // skip reserved
		for (unsigned iMid = 1; iMid < lastMember; iMid++) {
			if (ctx.opt_verbose >= ctx.VERBOSE_TICK && ctx.tick) {
				int perSecond = ctx.updateSpeed();

				if (perSecond == 0 || ctx.progress > ctx.progressHi) {
					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) | numMember=%u skipUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond, pStore->numMember, skipUnsafe, (double) ctx.cntCompare / ctx.cntHash);
				} else {
					int eta = (int) ((ctx.progressHi - ctx.progress) / perSecond);

					int etaH = eta / 3600;
					eta %= 3600;
					int etaM = eta / 60;
					eta %= 60;
					int etaS = eta;

					fprintf(stderr, "\r\e[K[%s] %lu(%7d/s) %.5f%% eta=%d:%02d:%02d | numMember=%u skipUnsafe=%u | hash=%.3f",
						ctx.timeAsString(), ctx.progress, perSecond, ctx.progress * 100.0 / ctx.progressHi, etaH, etaM, etaS, pStore->numMember, skipUnsafe, (double) ctx.cntCompare / ctx.cntHash);
				}

				ctx.tick = 0;
			}

			member_t *pMember = pStore->members + iMid;
			if (pMember->sid) {
				signature_t *pSignature = pStore->signatures + pMember->sid;

				// calculate head/tail
				tree.loadStringFast(pMember->name);
				findHeadTail(pMember, tree);

				if (!(pSignature->flags & signature_t::SIGMASK_SAFE)) {
					/*
					 * Adding (unsafe) member to unsafe group
					 */

					/*
					 * member should be unsafe
					 *
					 * @date 2021-06-23 09:46:35
					 *
					 * assert will fail when reading members from a list that is not properly ordered.
					 * and the list contains primes that are longer than the signatures.
					 * this will signatures to reject seeing primes as safe on the first pass.
					 * For the moment this is experimental code, so issue a warning instead of aborting a lengthy run
					 * (input list was ordered by sid instead of mid)
					 */
					// assert(!(pMember->flags & signature_t::SIGMASK_SAFE));
					if (pMember->flags & member_t::MEMMASK_SAFE) {
						fprintf(stderr,"\r\e[K[%s] WARNING: adding safe member %u:%s to unsafe signature %u:%s\n", ctx.timeAsString(), iMid, pMember->name, pMember->sid, pSignature->name);
						pSignature->flags |= signature_t::SIGMASK_SAFE;
					}

				} else if (!(pMember->flags & member_t::MEMMASK_SAFE)) {
					/*
					 * Reject adding unsafe member to safe group
					 */
					skipUnsafe++;
					ctx.progress++;
					continue;
				}

				// add to index
				unsigned ix = pStore->lookupMember(pMember->name);
				assert(pStore->memberIndex[ix] == 0);
				pStore->memberIndex[ix] = pStore->numMember;

				// add to group
				pMember->nextMember     = pSignature->firstMember;
				pSignature->firstMember = pStore->numMember;

				// copy
				pStore->members[pStore->numMember++] = *pMember;
			}

			ctx.progress++;
		}

		if (ctx.opt_verbose >= ctx.VERBOSE_TICK)
			fprintf(stderr, "\r\e[K");

		if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] Indexed members. numMember=%u skipUnsafe=%u\n",
				ctx.timeAsString(), pStore->numMember, skipUnsafe);

		/*
		 * Recalculate empty/unsafe groups
		 */

		numEmpty = numUnsafe = 0;
		for (unsigned iSid = 1; iSid < pStore->numSignature; iSid++) {
			if (pStore->signatures[iSid].firstMember == 0)
				numEmpty++;
			if (!(pStore->signatures[iSid].flags & signature_t::SIGMASK_SAFE))
				numUnsafe++;
		}

		if (numEmpty || numUnsafe) {
			if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
				fprintf(stderr, "[%s] WARNING: %u empty and %u unsafe signature groups\n", ctx.timeAsString(), numEmpty, numUnsafe);
		}

		/*
		 * Done
		 */
		if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] {\"numSlot\":%u,\"pure\":%u,\"interleave\":%u,\"numNode\":%u,\"numImprint\":%u,\"numSignature\":%u,\"numMember\":%u,\"numEmpty\":%u,\"numUnsafe\":%u}\n",
				ctx.timeAsString(), MAXSLOTS, (ctx.flags & context_t::MAGICMASK_PURE) ? 1 : 0, pStore->interleave, arg_numNodes, pStore->numImprint, pStore->numSignature, pStore->numMember, numEmpty, numUnsafe);

	}

};

/*
 * I/O context.
 * Needs to be global to be accessible by signal handlers.
 *
 * @global {context_t} I/O context
 */
context_t ctx;

/*
 * Application context.
 * Needs to be global to be accessible by signal handlers.
 *
 * @global {genmemberContext_t} Application context
 */
genmemberContext_t app(ctx);

/**
 * @date 2020-03-11 23:06:35
 *
 * Signal handler
 *
 * Delete partially created database unless explicitly requested
 *
 * @param {number} sig - signal (ignored)
 */
void sigintHandler(int __attribute__ ((unused)) sig) {
	if (app.arg_outputDatabase) {
		remove(app.arg_outputDatabase);
	}
	exit(1);
}

/**
 * @date 2020-03-11 23:06:35
 *
 * Signal handlers
 *
 * Bump interval timer
 *
 * @param {number} sig - signal (ignored)
 */
void sigalrmHandler(int __attribute__ ((unused)) sig) {
	if (ctx.opt_timer) {
		ctx.tick++;
		alarm(ctx.opt_timer);
	}
}

/**
 * @date 2020-03-14 11:17:04
 *
 * Program usage. Keep this directly above `main()`
 *
 * @param {string[]} argv - program arguments
 * @param {boolean} verbose - set to true for option descriptions
 * @param {userArguments_t} args - argument context
 */
void usage(char *argv[], bool verbose) {
	fprintf(stderr, "usage: %s <input.db> <numnode> [<output.db>]\n", argv[0]);

	if (verbose) {
		fprintf(stderr, "\n");
		fprintf(stderr, "\t   --force                         Force overwriting of database if already exists\n");
		fprintf(stderr, "\t   --[no-]generate                 Invoke generator for new candidates [default=%s]\n", app.opt_generate ? "enabled" : "disabled");
		fprintf(stderr, "\t-h --help                          This list\n");
		fprintf(stderr, "\t   --imprintindexsize=<number>     Size of imprint index [default=%u]\n", app.opt_imprintIndexSize);
		fprintf(stderr, "\t   --interleave=<number>           Imprint index interleave [default=%u]\n", app.opt_interleave);
		fprintf(stderr, "\t   --load=<file>                   Read candidates from file instead of generating [default=%s]\n", app.opt_load ? app.opt_load : "");
		fprintf(stderr, "\t   --maximprint=<number>           Maximum number of imprints [default=%u]\n", app.opt_maxImprint);
		fprintf(stderr, "\t   --maxmember=<number>            Maximum number of members [default=%u]\n", app.opt_maxMember);
		fprintf(stderr, "\t   --memberindexsize=<number>      Size of member index [default=%u]\n", app.opt_memberIndexSize);
		fprintf(stderr, "\t   --[no-]paranoid                 Enable expensive assertions [default=%s]\n", (ctx.flags & context_t::MAGICMASK_PARANOID) ? "enabled" : "disabled");
		fprintf(stderr, "\t   --[no-]pure                     QTF->QnTF rewriting [default=%s]\n", (ctx.flags & context_t::MAGICMASK_PURE) ? "enabled" : "disabled");
		fprintf(stderr, "\t-q --quiet                         Say less\n");
		fprintf(stderr, "\t   --ratio=<number>                Index/data ratio [default=%.1f]\n", app.opt_ratio);
		fprintf(stderr, "\t   --[no-]saveindex                Save with indices [default=%s]\n", app.opt_saveIndex ? "enabled" : "disabled");
		fprintf(stderr, "\t   --sid=[<low>,]<high>            Sid range upper bound  [default=%u,%u]\n", app.opt_sidLo, app.opt_sidHi);
		fprintf(stderr, "\t   --signatureindexsize=<number>   Size of signature index [default=%u]\n", app.opt_signatureIndexSize);
		fprintf(stderr, "\t   --task=sge                      Get task settings from SGE environment\n");
		fprintf(stderr, "\t   --task=<id>,<last>              Task id/number of tasks. [default=%u,%u]\n", app.opt_taskId, app.opt_taskLast);
		fprintf(stderr, "\t   --text                          Textual output instead of binary database\n");
		fprintf(stderr, "\t   --timer=<seconds>               Interval timer for verbose updates [default=%u]\n", ctx.opt_timer);
		fprintf(stderr, "\t   --[no-]unsafe                   Reindex imprints based on empty/unsafe signature groups [default=%s]\n", (ctx.flags & context_t::MAGICMASK_UNSAFE) ? "enabled" : "disabled");
		fprintf(stderr, "\t-v --truncate                      Truncate on database overflow\n");
		fprintf(stderr, "\t-v --verbose                       Say more\n");
		fprintf(stderr, "\t   --window=[<low>,]<high>         Upper end restart window [default=%lu,%lu]\n", app.opt_windowLo, app.opt_windowHi);
	}
}

/**
 * @date 2020-03-14 11:19:40
 *
 * Program main entry point
 * Process all user supplied arguments to construct a application context.
 * Activate application context.
 *
 * @param  {number} argc - number of arguments
 * @param  {string[]} argv - program arguments
 * @return {number} 0 on normal return, non-zero when attention is required
 */
int main(int argc, char *argv[]) {
	setlinebuf(stdout);

	/*
	 *  Process program options
	 */
	for (;;) {
		// Long option shortcuts
		enum {
			// long-only opts
			LO_DEBUG   = 1,
			LO_FORCE,
			LO_GENERATE,
			LO_IMPRINTINDEXSIZE,
			LO_INTERLEAVE,
			LO_LOAD,
			LO_MAXIMPRINT,
			LO_MAXMEMBER,
			LO_MEMBERINDEXSIZE,
			LO_NOGENERATE,
			LO_NOPARANOID,
			LO_NOPURE,
			LO_NOSAVEINDEX,
			LO_NOUNSAFE,
			LO_PARANOID,
			LO_PURE,
			LO_RATIO,
			LO_SAVEINDEX,
			LO_SID,
			LO_SIGNATUREINDEXSIZE,
			LO_TASK,
			LO_TEXT,
			LO_TIMER,
			LO_TRUNCATE,
			LO_UNSAFE,
			LO_WINDOW,
			// short opts
			LO_HELP    = 'h',
			LO_QUIET   = 'q',
			LO_VERBOSE = 'v',
		};

		// long option descriptions
		static struct option long_options[] = {
			/* name, has_arg, flag, val */
			{"debug",              1, 0, LO_DEBUG},
			{"force",              0, 0, LO_FORCE},
			{"generate",           0, 0, LO_GENERATE},
			{"help",               0, 0, LO_HELP},
			{"imprintindexsize",   1, 0, LO_IMPRINTINDEXSIZE},
			{"interleave",         1, 0, LO_INTERLEAVE},
			{"load",               1, 0, LO_LOAD},
			{"maximprint",         1, 0, LO_MAXIMPRINT},
			{"maxmember",          1, 0, LO_MAXMEMBER},
			{"memberindexsize",    1, 0, LO_MEMBERINDEXSIZE},
			{"no-generate",        0, 0, LO_NOGENERATE},
			{"no-paranoid",        0, 0, LO_NOPARANOID},
			{"no-pure",            0, 0, LO_NOPURE},
			{"no-saveindex",       0, 0, LO_NOSAVEINDEX},
			{"no-unsafe",          0, 0, LO_NOUNSAFE},
			{"paranoid",           0, 0, LO_PARANOID},
			{"pure",               0, 0, LO_PURE},
			{"quiet",              2, 0, LO_QUIET},
			{"ratio",              1, 0, LO_RATIO},
			{"saveindex",          0, 0, LO_SAVEINDEX},
			{"sid",                1, 0, LO_SID},
			{"signatureindexsize", 1, 0, LO_SIGNATUREINDEXSIZE},
			{"task",               1, 0, LO_TASK},
			{"text",               2, 0, LO_TEXT},
			{"timer",              1, 0, LO_TIMER},
			{"truncate",           0, 0, LO_TRUNCATE},
			{"unsafe",             0, 0, LO_UNSAFE},
			{"verbose",            2, 0, LO_VERBOSE},
			{"window",             1, 0, LO_WINDOW},
			//
			{NULL,                 0, 0, 0}
		};

		char optstring[64];
		char *cp          = optstring;
		int  option_index = 0;

		/* construct optarg */
		for (int i = 0; long_options[i].name; i++) {
			if (isalpha(long_options[i].val)) {
				*cp++ = (char) long_options[i].val;

				if (long_options[i].has_arg != 0)
					*cp++ = ':';
				if (long_options[i].has_arg == 2)
					*cp++ = ':';
			}
		}

		*cp = '\0';

		// parse long options
		int c = getopt_long(argc, argv, optstring, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case LO_DEBUG:
			ctx.opt_debug = ::strtoul(optarg, NULL, 0);
			break;
		case LO_FORCE:
			app.opt_force++;
			break;
		case LO_GENERATE:
			app.opt_generate++;
			break;
		case LO_HELP:
			usage(argv, true);
			exit(0);
		case LO_IMPRINTINDEXSIZE:
			app.opt_imprintIndexSize = ctx.nextPrime(::strtod(optarg, NULL));
			break;
		case LO_INTERLEAVE:
			app.opt_interleave = ::strtoul(optarg, NULL, 0);
			if (!getMetricsInterleave(MAXSLOTS, app.opt_interleave))
				ctx.fatal("--interleave must be one of [%s]\n", getAllowedInterleaves(MAXSLOTS));
			break;
		case LO_LOAD:
			app.opt_load = optarg;
			break;
		case LO_MAXIMPRINT:
			app.opt_maxImprint = ctx.dToMax(::strtod(optarg, NULL));
			break;
		case LO_MAXMEMBER:
			app.opt_maxMember = ctx.dToMax(::strtod(optarg, NULL));
			break;
		case LO_MEMBERINDEXSIZE:
			app.opt_memberIndexSize = ctx.nextPrime(::strtod(optarg, NULL));
			break;
		case LO_NOGENERATE:
			app.opt_generate = 0;
			break;
		case LO_NOPARANOID:
			ctx.flags &= ~context_t::MAGICMASK_PARANOID;
			break;
		case LO_NOPURE:
			ctx.flags &= ~context_t::MAGICMASK_PURE;
			break;
		case LO_NOUNSAFE:
			ctx.flags &= ~context_t::MAGICMASK_UNSAFE;
			break;
		case LO_PARANOID:
			ctx.flags |= context_t::MAGICMASK_PARANOID;
			break;
		case LO_PURE:
			ctx.flags |= context_t::MAGICMASK_PURE;
			break;
		case LO_QUIET:
			ctx.opt_verbose = optarg ? ::strtoul(optarg, NULL, 0) : ctx.opt_verbose - 1;
			break;
		case LO_RATIO:
			app.opt_ratio = strtof(optarg, NULL);
			break;
		case LO_NOSAVEINDEX:
			app.opt_saveIndex = 0;
			break;
		case LO_SAVEINDEX:
			app.opt_saveIndex = optarg ? ::strtoul(optarg, NULL, 0) : app.opt_saveIndex + 1;
			break;
		case LO_SID: {
			unsigned m, n;

			int ret = sscanf(optarg, "%u,%u", &m, &n);
			if (ret == 2) {
				app.opt_sidLo = m;
				app.opt_sidHi = n;
			} else if (ret == 1) {
				app.opt_sidHi = m;
			} else {
				usage(argv, true);
				exit(1);
			}

			break;
		}
		case LO_SIGNATUREINDEXSIZE:
			app.opt_signatureIndexSize = ctx.nextPrime(::strtod(optarg, NULL));
			break;
		case LO_TASK:
			if (::strcmp(optarg, "sge") == 0) {
				const char *p;

				p = getenv("SGE_TASK_ID");
				app.opt_taskId = p ? atoi(p) : 0;
				if (app.opt_taskId < 1) {
					fprintf(stderr, "Missing environment SGE_TASK_ID\n");
					exit(0);
				}

				p = getenv("SGE_TASK_LAST");
				app.opt_taskLast = p ? atoi(p) : 0;
				if (app.opt_taskLast < 1) {
					fprintf(stderr, "Missing environment SGE_TASK_LAST\n");
					exit(0);
				}

				if (app.opt_taskId < 1 || app.opt_taskId > app.opt_taskLast) {
					fprintf(stderr, "sge id/last out of bounds: %u,%u\n", app.opt_taskId, app.opt_taskLast);
					exit(1);
				}

				// set ticker interval to 60 seconds
				ctx.opt_timer = 60;
			} else {
				if (sscanf(optarg, "%u,%u", &app.opt_taskId, &app.opt_taskLast) != 2) {
					usage(argv, true);
					exit(1);
				}
				if (app.opt_taskId == 0 || app.opt_taskLast == 0) {
					fprintf(stderr, "Task id/last must be non-zero\n");
					exit(1);
				}
				if (app.opt_taskId > app.opt_taskLast) {
					fprintf(stderr, "Task id exceeds last\n");
					exit(1);
				}
			}
			break;
		case LO_TEXT:
			app.opt_text = optarg ? ::strtoul(optarg, NULL, 0) : app.opt_text + 1;
			break;
		case LO_TIMER:
			ctx.opt_timer = ::strtoul(optarg, NULL, 0);
			break;
		case LO_TRUNCATE:
			app.opt_truncate = optarg ? ::strtoul(optarg, NULL, 0) : app.opt_truncate + 1;
			break;
		case LO_UNSAFE:
			ctx.flags |= context_t::MAGICMASK_UNSAFE;
			break;
		case LO_VERBOSE:
			ctx.opt_verbose = optarg ? ::strtoul(optarg, NULL, 0) : ctx.opt_verbose + 1;
			break;
		case LO_WINDOW: {
			uint64_t m, n;

			int ret = sscanf(optarg, "%lu,%lu", &m, &n);
			if (ret == 2) {
				app.opt_windowLo = m;
				app.opt_windowHi = n;
			} else if (ret == 1) {
				app.opt_windowHi = m;
			} else {
				usage(argv, true);
				exit(1);
			}

			break;
		}

		case '?':
			fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
			exit(1);
		default:
			fprintf(stderr, "getopt_long() returned character code %d\n", c);
			exit(1);
		}
	}

	/*
	 * Program arguments
	 */
	if (argc - optind >= 1)
		app.arg_inputDatabase = argv[optind++];

	if (argc - optind >= 1) {
		char *endptr;

		errno = 0; // To distinguish success/failure after call
		app.arg_numNodes = ::strtoul(argv[optind++], &endptr, 0);

		// strip trailing spaces
		while (*endptr && isspace(*endptr))
			endptr++;

		// test for error
		if (errno != 0 || *endptr != '\0')
			app.arg_inputDatabase = NULL;
	}

	if (argc - optind >= 1)
		app.arg_outputDatabase = argv[optind++];

	if (app.arg_inputDatabase == NULL) {
		usage(argv, false);
		exit(1);
	}

	/*
	 * `--task` post-processing
	 */
	if (app.opt_taskId || app.opt_taskLast) {
		const metricsGenerator_t *pMetrics = getMetricsGenerator(MAXSLOTS, ctx.flags & context_t::MAGICMASK_PURE, app.arg_numNodes);
		if (!pMetrics)
			ctx.fatal("no preset for --task\n");

		// split progress into chunks
		uint64_t taskSize = pMetrics->numProgress / app.opt_taskLast;
		if (taskSize == 0)
			taskSize = 1;
		app.opt_windowLo = taskSize * (app.opt_taskId - 1);
		app.opt_windowHi = taskSize * app.opt_taskId;

		// last task is open ended in case metrics are off
		if (app.opt_taskId == app.opt_taskLast)
			app.opt_windowHi = 0;
	}
	if (app.opt_windowHi && app.opt_windowLo >= app.opt_windowHi) {
		fprintf(stderr, "--window low exceeds high\n");
		exit(1);
	}

	if (app.opt_windowLo || app.opt_windowHi) {
		if (app.arg_numNodes > tinyTree_t::TINYTREE_MAXNODES || restartIndex[app.arg_numNodes][(ctx.flags & context_t::MAGICMASK_PURE) ? 1 : 0] == 0) {
			fprintf(stderr, "No restart data for --window\n");
			exit(1);
		}
	}

	/*
	 * None of the outputs may exist
	 */

	if (app.arg_outputDatabase && !app.opt_force) {
		struct stat sbuf;

		if (!stat(app.arg_outputDatabase, &sbuf)) {
			fprintf(stderr, "%s already exists. Use --force to overwrite\n", app.arg_outputDatabase);
			exit(1);
		}
	}

	if (app.opt_load) {
		struct stat sbuf;

		if (stat(app.opt_load, &sbuf)) {
			fprintf(stderr, "%s does not exist\n", app.opt_load);
			exit(1);
		}
	}

	if (app.opt_text && isatty(1)) {
		fprintf(stderr, "stdout not redirected\n");
		exit(1);
	}

	// register timer handler
	if (ctx.opt_timer) {
		signal(SIGALRM, sigalrmHandler);
		::alarm(ctx.opt_timer);
	}

	/*
	 * Open input and create output database
	 */

	// Open input
	database_t db(ctx);

	// test readOnly mode
	app.readOnlyMode = (app.arg_outputDatabase == NULL && app.opt_text != app.OPTTEXT_BRIEF && app.opt_text != app.OPTTEXT_VERBOSE && app.opt_text != app.OPTTEXT_SQL);

	db.open(app.arg_inputDatabase, !app.readOnlyMode);

	// display system flags when database was created
	if (ctx.opt_verbose >= ctx.VERBOSE_WARNING) {
		char dbText[128], ctxText[128];

		ctx.flagsToText(db.creationFlags, dbText);
		ctx.flagsToText(ctx.flags, ctxText);

		if (db.creationFlags != ctx.flags)
			fprintf(stderr, "[%s] WARNING: Database/system flags differ: database=[%s] current=[%s]\n", ctx.timeAsString(), dbText, ctxText);
		else if (db.creationFlags && ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
			fprintf(stderr, "[%s] FLAGS [%s]\n", ctx.timeAsString(), dbText);
	}

	if (ctx.opt_verbose >= ctx.VERBOSE_VERBOSE)
		fprintf(stderr, "[%s] %s\n", ctx.timeAsString(), json_dumps(db.jsonInfo(NULL), JSON_PRESERVE_ORDER | JSON_COMPACT));

	/*
	 * @date 2020-04-21 00:16:34
	 *
	 * create output
	 *
	 * Transforms, signature, hint and imprint data never change and can be inherited
	 * Members can be inherited when nothing is added (missing output database)
	 *
	 * Sections can be inherited if their data or index settings remain unchanged
	 *
	 * NOTE: Signature data must be writable when `firstMember` changes (output database present)
	 */

	database_t store(ctx);

	// will be using `lookupSignature()`, `lookupImprintAssociative()` and `lookupMember()`
	app.inheritSections &= ~(database_t::ALLOCMASK_SIGNATURE | database_t::ALLOCMASK_MEMBER | database_t::ALLOCMASK_MEMBERINDEX);
	// signature indices are used read-only, remove from inherit if sections are empty
	if (!db.signatureIndexSize)
		app.inheritSections &= ~database_t::ALLOCMASK_SIGNATUREINDEX;
	if (!db.numImprint)
		app.inheritSections &= ~database_t::ALLOCMASK_IMPRINT;
	if (!db.imprintIndexSize)
		app.inheritSections &= ~database_t::ALLOCMASK_IMPRINTINDEX;
	// `--unsafe` requires rebuilding of imprints
	if (ctx.flags & context_t::MAGICMASK_UNSAFE)
		app.rebuildSections |= database_t::ALLOCMASK_IMPRINT | database_t::ALLOCMASK_IMPRINTINDEX;
	// will require local copy of signatures
	app.rebuildSections |= database_t::ALLOCMASK_SIGNATURE;

	// input database will always have a minimal node size of 4.
	unsigned minNodes = app.arg_numNodes > 4 ? app.arg_numNodes : 4;

	// inherit signature size
	if (!app.readOnlyMode)
		app.opt_maxSignature = db.numSignature;

	// assign sizes to output sections
	app.sizeDatabaseSections(store, db, minNodes);

	/*
	 * Finalise allocations and create database
	 */

	// allocate evaluators
	app.pEvalFwd    = (footprint_t *) ctx.myAlloc("genmemberContext_t::pEvalFwd", tinyTree_t::TINYTREE_NEND * MAXTRANSFORM, sizeof(*app.pEvalFwd));
	app.pEvalRev    = (footprint_t *) ctx.myAlloc("genmemberContext_t::pEvalRev", tinyTree_t::TINYTREE_NEND * MAXTRANSFORM, sizeof(*app.pEvalRev));
	app.pSafeScores = (uint16_t *) ctx.myAlloc("genmemberContext_t::pMemberScores", store.maxSignature, sizeof(*app.pSafeScores));

	if (ctx.opt_verbose >= ctx.VERBOSE_WARNING) {
		// Assuming with database allocations included
		size_t allocated = ctx.totalAllocated + store.estimateMemoryUsage(app.inheritSections);

		struct sysinfo info;
		if (sysinfo(&info) == 0) {
			double percent = 100.0 * allocated / info.freeram;
			if (percent > 80)
				fprintf(stderr, "WARNING: using %.1f%% of free memory minus cache\n", percent);
		}
	}

	// actual create
	store.create(app.inheritSections);
	app.pStore = &store;

	if (ctx.opt_verbose >= ctx.VERBOSE_ACTIONS && !(app.rebuildSections & ~app.inheritSections)) {
		struct sysinfo info;
		if (sysinfo(&info) != 0)
			info.freeram = 0;

		fprintf(stderr, "[%s] Allocated %.3fG memory. freeMemory=%.3fG.\n", ctx.timeAsString(), ctx.totalAllocated / 1e9, info.freeram / 1e9);
	}

	// initialize evaluator early using input database
	tinyTree_t tree(ctx);
	tree.initialiseVector(ctx, app.pEvalFwd, MAXTRANSFORM, db.fwdTransformData);
	tree.initialiseVector(ctx, app.pEvalRev, MAXTRANSFORM, db.revTransformData);

	for (unsigned iSid = 0; iSid < store.maxSignature; iSid++)
		app.pSafeScores[iSid] = 0;

	// calc initial signature group scores (may differ from signature)
	for (unsigned iSid = 0; iSid < store.numSignature; iSid++) {
		const signature_t *pSignature = db.signatures + iSid;

		if (pSignature->flags & signature_t::SIGMASK_SAFE) {
			assert(pSignature->firstMember);

			const member_t *pMember = db.members + pSignature->firstMember;

			if (app.opt_score) {
				app.pSafeScores[iSid] = tinyTree_t::calcScoreName(pMember->name);
			} else {
				tinyTree_t tree(ctx);
				tree.loadStringFast(pMember->name);

				app.pSafeScores[iSid] = tree.count - tinyTree_t::TINYTREE_NSTART;
			}
		}
	}

	/*
	 * Inherit/copy sections
	 */

	app.populateDatabaseSections(store, db);

	/*
	 * Rebuild sections
	 */

	// todo: move this to `populateDatabaseSections()`
	// data sections cannot be automatically rebuilt
	assert((app.rebuildSections & (database_t::ALLOCMASK_SWAP | database_t::ALLOCMASK_HINT | database_t::ALLOCMASK_MEMBER)) == 0);

	if (app.rebuildSections & database_t::ALLOCMASK_SIGNATURE) {
		store.numSignature = db.numSignature;
		::memcpy(store.signatures, db.signatures, store.numSignature * sizeof(*store.signatures));
	}
	if (app.rebuildSections & database_t::ALLOCMASK_IMPRINT) {
		// rebuild imprints
		if (!(ctx.flags & context_t::MAGICMASK_UNSAFE)) {
			// regular rebuild
			app.rebuildImprints(0);
		} else if (store.numHint > 1) {
			// rebuild unsafe with hints
			app.rebuildImprintsWithHints();
		} else {
			// rebuild unsage with sid bounds
			app.rebuildImprints(ctx.flags & context_t::MAGICMASK_UNSAFE);
		}
		app.rebuildSections &= ~(database_t::ALLOCMASK_IMPRINT | database_t::ALLOCMASK_IMPRINTINDEX);
	}
	if (app.rebuildSections)
		store.rebuildIndices(app.rebuildSections);

	/*
	 * count empty/unsafe
	 */

	app.numEmpty = app.numUnsafe = 0;
	for (unsigned iSid = 1; iSid < store.numSignature; iSid++) {
		if (store.signatures[iSid].firstMember == 0)
			app.numEmpty++;
		if (!(store.signatures[iSid].flags & signature_t::SIGMASK_SAFE))
			app.numUnsafe++;
	}

	if (ctx.opt_verbose >= ctx.VERBOSE_SUMMARY)
		fprintf(stderr, "[%s] numImprint=%u(%.0f%%) numMember=%u(%.0f%%) numEmpty=%u numUnsafe=%u\n",
			ctx.timeAsString(),
			store.numImprint, store.numImprint * 100.0 / store.maxImprint,
			store.numMember, store.numMember * 100.0 / store.maxMember,
			app.numEmpty, app.numUnsafe - app.numEmpty);

	/*
	 * Where to look for new candidates
	 */

	// if input is empty, skip reserved entries
	if (!app.readOnlyMode) {
		assert(store.numMember > 0);
	}

	if (app.opt_load)
		app.membersFromFile();
	if (app.opt_generate) {
		if (app.arg_numNodes == 1) {
			// also include "0" and "a"
			app.arg_numNodes = 0;
			app.membersFromGenerator();
			app.arg_numNodes = 1;
		}
		app.membersFromGenerator();
	}

	/*
	 * re-order and re-index members
	 */

	if (!app.readOnlyMode) {
		// compact, sort and reindex members
		app.finaliseMembers();

		/*
		 * Check that all unsafe groups have no safe members (or the group would have been safe)
		 */
		for (unsigned iSid = 1; iSid < store.numSignature; iSid++) {
			if (!(store.signatures[iSid].flags & signature_t::SIGMASK_SAFE)) {
				for (unsigned iMid = store.signatures[iSid].firstMember; iMid; iMid = store.members[iMid].nextMember) {
					assert(!(store.members[iMid].flags & member_t::MEMMASK_SAFE));
				}
			}
		}

		if (app.opt_text == app.OPTTEXT_BRIEF) {
			/*
			 * Display members of complete dataset
			 *
			 * <memberName> <numPlaceholder>
			 */
			for (unsigned iMid = 1; iMid < store.numMember; iMid++)
				printf("%s\n", store.members[iMid].name);
		}

		if (app.opt_text == app.OPTTEXT_VERBOSE) {
			/*
			 * Display full members, grouped by signature
			 */
			for (unsigned iSid = 1; iSid < store.numSignature; iSid++) {
				const signature_t *pSignature = store.signatures + iSid;

				for (unsigned iMid = pSignature->firstMember; iMid; iMid = store.members[iMid].nextMember) {
					member_t *pMember = store.members + iMid;

					printf("%u\t%u\t%u\t%s\t", iMid, iSid, pMember->tid, pMember->name);
					printf("0x%03x\t", tinyTree_t::calcScoreName(pMember->name));

					printf("%u:%s\t", pMember->Q, store.members[pMember->Q].name);
					printf("%u:%s\t", pMember->T, store.members[pMember->T].name);
					printf("%u:%s\t", pMember->F, store.members[pMember->F].name);

					for (unsigned i = 0; i < member_t::MAXHEAD; i++)
						printf("%u:%s\t", pMember->heads[i], store.members[pMember->heads[i]].name);

					if (pMember->flags & member_t::MEMMASK_SAFE)
						printf("S");
					else
						printf("N");
					if (pSignature->flags & signature_t::SIGMASK_SAFE)
						printf("S");
					else
						printf("N");
					printf("\n");
				}
			}
		}
		if (app.opt_text == app.OPTTEXT_SQL) {
			/*
			 * Display full members, grouped by signature
			 */
			for (unsigned iSid = 1; iSid < store.numSignature; iSid++) {
				const signature_t *pSignature = store.signatures + iSid;

				for (unsigned iMid = pSignature->firstMember; iMid; iMid = store.members[iMid].nextMember) {
					member_t *pMember = store.members + iMid;

					if (pMember->flags & member_t::MEMMASK_SAFE) {
						printf("insert ignore into member (mid,sid,tid,score,name,q,t,f,h1,h2,h3,h4,h5,h6) values (%u,%u,%u,%x,\"%s\",%u,%u,%u, %u,%u,%u,%u,%u,%u);\n",
						       iMid, pMember->sid, pMember->tid, tinyTree_t::calcScoreName(pMember->name), pMember->name,
						       pMember->Q, pMember->T, pMember->F,
						       pMember->heads[0], pMember->heads[1], pMember->heads[2], pMember->heads[3], pMember->heads[4], pMember->heads[5]);
					}
				}
			}
		}
	}

	/*
	 * Save the database
	 */

	if (app.arg_outputDatabase) {
		if (!app.opt_saveIndex) {
			store.signatureIndexSize = 0;
			store.hintIndexSize      = 0;
			store.imprintIndexSize   = 0;
			store.numImprint         = 0;
			store.interleave         = 0;
			store.interleaveStep     = 0;
		}

		// unexpected termination should unlink the outputs
		signal(SIGINT, sigintHandler);
		signal(SIGHUP, sigintHandler);

		store.save(app.arg_outputDatabase);
	}

	if (ctx.opt_verbose >= ctx.VERBOSE_WARNING) {
		json_t *jResult = json_object();
		json_object_set_new_nocheck(jResult, "done", json_string_nocheck(argv[0]));
		if (app.opt_taskLast) {
			json_object_set_new_nocheck(jResult, "taskId", json_integer(app.opt_taskId));
			json_object_set_new_nocheck(jResult, "taskLast", json_integer(app.opt_taskLast));
		}
		if (app.opt_windowLo || app.opt_windowHi) {
			json_object_set_new_nocheck(jResult, "windowLo", json_integer(app.opt_windowLo));
			json_object_set_new_nocheck(jResult, "windowHi", json_integer(app.opt_windowHi));
		}
		if (app.arg_outputDatabase)
			json_object_set_new_nocheck(jResult, "filename", json_string_nocheck(app.arg_outputDatabase));
		store.jsonInfo(jResult);
		fprintf(stderr, "%s\n", json_dumps(jResult, JSON_PRESERVE_ORDER | JSON_COMPACT));
	}

	return 0;
}
