#ifndef _DBTOOL_H
#define _DBTOOL_H

/*
 * @date 2020-04-27 18:47:26
 *
 * A collection of utilities shared across database creation tools `gensignature`, `genhint`, `genmember` and more.
 *
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
#include "context.h"
#include "database.h"
#include "generator.h"
#include "metrics.h"

struct dbtool_t : callable_t {

	/*
	 * @date 2020-03-25 15:06:54
	 */
	enum {
		/*
		 * @date 2020-03-30 16:17:28
		 *
		 * default interleave (taken from `ratioMetrics_X[]`)
		 * In general 504 seems to be best choice
		 * However, with 4-nodes, 120 is just as fast as 504 but uses half storage.
		 * With 4n9-i120 imprint storage is 8G. On machines with 32G memory this gives about 4 workers with each 4G local and 8G shared memory
		 *
		 * @date 2020-04-04 20:56:35
		 *
		 * After experience, 504 is definitely faster
		 */
		METRICS_DEFAULT_INTERLEAVE = 504,

		// default ratio (taken from `ratioMetrics_X[]`). NOTE: Times 10!
		METRICS_DEFAULT_RATIO = 50, // NOTE: Its actually 5.0
	};

	/// @var {context_t} I/O context
	context_t &ctx;

	/// @var {number} size of imprint index WARNING: must be prime
	unsigned opt_imprintIndexSize;
	/// @var {number} size of hint index WARNING: must be prime
	unsigned opt_hintIndexSize;
	/// @var {number} interleave for associative imprint index
	unsigned opt_interleave;
	/// @var {number} Maximum number of hints to be stored database
	unsigned opt_maxHint;
	/// @var {number} Maximum number of imprints to be stored database
	unsigned opt_maxImprint;
	/// @var {number} Maximum number of members to be stored database
	unsigned opt_maxMember;
	/// @var {number} Maximum number of signatures to be stored database
	unsigned opt_maxSignature;
	/// @var {number} size of member index WARNING: must be prime
	unsigned opt_memberIndexSize;
	/// @var {number} index/data ratio
	double opt_ratio;
	/// @var {number} size of signature index WARNING: must be prime
	unsigned opt_signatureIndexSize;

	/// @var {number} "0" assume input is read-only, else input is copy-on-write.
	unsigned copyOnWrite;
	/// @var {number} may/maynot make changes to database
	unsigned readOnlyMode;
	// allocated sections that need rebuilding
	unsigned rebuildSections;
	// mmapped sections that are copy-on-write
	unsigned inheritSections;

	/**
	 * Constructor
	 */
	dbtool_t(context_t &ctx) : ctx(ctx) {
		// arguments and options
		opt_imprintIndexSize = 0;
		opt_hintIndexSize = 0;
		opt_interleave = 0;
		opt_maxHint = 0;
		opt_maxImprint = 0;
		opt_maxMember = 0;
		opt_maxSignature = 0;
		opt_memberIndexSize = 0;
		opt_ratio = METRICS_DEFAULT_RATIO / 10.0;
		opt_signatureIndexSize = 0;

		copyOnWrite = 0;
		inheritSections = database_t::ALLOCMASK_TRANSFORM |
		                  database_t::ALLOCMASK_SIGNATURE | database_t::ALLOCMASK_SIGNATUREINDEX |
		                  database_t::ALLOCMASK_HINT | database_t::ALLOCMASK_HINTINDEX |
		                  database_t::ALLOCMASK_IMPRINT | database_t::ALLOCMASK_IMPRINTINDEX |
		                  database_t::ALLOCMASK_MEMBER | database_t::ALLOCMASK_MEMBERINDEX;
		readOnlyMode = 0;
		rebuildSections = 0;
	}

	/**
	 * @date 2020-04-25 00:05:32
	 *
	 * NOTE: `signatureIndex`, `hintIndex` and `imprintIndex` are first-level indices derived from `signatures`, `hints` and `imprints`.
	 *       `imprints` is a second-level index derived from `signatures`
	 *
	 * workflow:
	 *   - No output specified make primary sections/indices secondary
	 *   - Size output sections according to command-line overrides
	 *   - If none given for primary sections (signatures,imprints) take from metrics
	 *   - If none given for secondary sections (hints) inherit from input database
	 *   - Any changes that change the hashing properties of indices invalidate them and require rebuilding
	 *   - Any primary section/index have their contents copied
	 *   - Any secondary section/index that remain same size get inherited
	 *   - All indices must have at least one entry more then their data
	 *   - All primary sections must have at least the reserved first entry
	 *   - Any secondary section may have zero entries
	 *
	 * @date 2020-04-21 19:59:47
	 *
	 * if (inheritSection)
	 *   inherit();
	 * else if (rebuildSection)
	 *   rebuild();
	 * else
	 *   copy();
	 *
	 * @param {database_t} store - writable output database
	 * @param {database_t} db - read-only input database
	 * @param {number} numNodes - to find matching metrics
	 */
	void __attribute__((optimize("O0"))) sizeDatabaseSections(database_t &store, const database_t &db, unsigned numNodes) {

		/*
		 * @date 2020-03-17 13:57:25
		 *
		 * Database indices are hashlookup tables with overflow.
		 * The art is to have a hash function that distributes evenly over the hashtable.
		 * If index entries are in use, then jump to overflow entries.
		 * The larger the index in comparison to the number of data entries the lower the chance an overflow will occur.
		 * The ratio between index and data size is called `ratio`.
		 */

		inheritSections &= ~rebuildSections;

		/*
		 * signature
		 */

		// data
		if (this->opt_maxSignature) {
			// user specified
			store.maxSignature = this->opt_maxSignature;
		} else if (inheritSections & database_t::ALLOCMASK_SIGNATURE) {
			// inherited. pass-though
			store.maxSignature = db.numSignature;
		} else if (!readOnlyMode) {
			// resize using metrics
			const metricsGenerator_t *pMetrics = getMetricsGenerator(MAXSLOTS, ctx.flags & ctx.MAGICMASK_PURE, numNodes);
			if (!pMetrics)
				ctx.fatal("no preset for --maxsignature\n");

			// give metrics a margin of error
			store.maxSignature = ctx.raisePercent(pMetrics->numSignature, 5);
		} else if (db.numSignature) {
			// non-empty. pass-though
			store.maxSignature = db.numSignature;
		} else {
			// empty. create minimal sized section
			store.maxSignature = 1;
		}

		if (store.maxSignature > db.numSignature) {
			// disable inherit when section wants to grow
			inheritSections &= ~database_t::ALLOCMASK_SIGNATURE;
		} else if (this->copyOnWrite) {
			// inherit when section fits and copy-on-write
			inheritSections |= database_t::ALLOCMASK_SIGNATURE;
		}

		// index
		if (!store.maxSignature) {
			// no data to index
			store.signatureIndexSize = 0;
		} else {
			if (this->opt_signatureIndexSize) {
				// user specified
				store.signatureIndexSize = this->opt_signatureIndexSize;
			} else if (inheritSections & database_t::ALLOCMASK_SIGNATUREINDEX) {
				// inherited. pass-though
				store.signatureIndexSize = db.signatureIndexSize;
			} else if (!readOnlyMode) {
				// auto-resize
				store.signatureIndexSize = ctx.nextPrime(store.maxSignature * this->opt_ratio);
			} else if (db.signatureIndexSize) {
				// non-empty. pass-though
				store.signatureIndexSize = db.signatureIndexSize;
			} else {
				// empty. create minimal sized section
				store.signatureIndexSize = 1;
			}

			if (store.signatureIndexSize != db.signatureIndexSize) {
				// source section is missing or unusable
				rebuildSections |= database_t::ALLOCMASK_SIGNATUREINDEX;
				inheritSections &= ~rebuildSections;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_SIGNATUREINDEX;
			}
		}

		/*
		 * hint
		 */

		// data
		if (this->opt_maxHint) {
			// user specified
			store.maxHint = this->opt_maxHint;
		} else if (inheritSections & database_t::ALLOCMASK_HINT) {
			// inherited. pass-though
			store.maxHint = db.numHint;
		} else if (!readOnlyMode) {
			// resize using metrics
			const metricsGenerator_t *pMetrics = getMetricsGenerator(MAXSLOTS, ctx.flags & ctx.MAGICMASK_PURE, numNodes);
			if (!pMetrics)
				ctx.fatal("no preset for --maxhint\n");

			// give metrics a margin of error
			store.maxHint = ctx.raisePercent(pMetrics->numHint, 5);
		} else if (db.numHint) {
			// non-empty. pass-though
			store.maxHint = db.numHint;
		} else {
			// empty. create minimal sized section
			store.maxHint = 1;
		}

		if (store.maxHint > db.numHint) {
			// disable inherit when section wants to grow
			inheritSections &= ~database_t::ALLOCMASK_HINT;
		} else if (this->copyOnWrite) {
			// inherit when section fits and copy-on-write
			inheritSections |= database_t::ALLOCMASK_HINT;
		}

		// index
		if (!store.maxHint) {
			// no data to index
			store.hintIndexSize = 0;
		} else {
			if (this->opt_hintIndexSize) {
				// user specified
				store.hintIndexSize = this->opt_hintIndexSize;
			} else if (inheritSections & database_t::ALLOCMASK_HINTINDEX) {
				// inherited. pass-though
				store.hintIndexSize = db.hintIndexSize;
			} else if (!readOnlyMode) {
				// auto-resize
				store.hintIndexSize = ctx.nextPrime(store.maxHint * this->opt_ratio);
			} else if (db.hintIndexSize) {
				// non-empty. pass-though
				store.hintIndexSize = db.hintIndexSize;
			} else {
				// empty. create minimal sized section
				store.hintIndexSize = 1;
			}

			if (store.hintIndexSize != db.hintIndexSize) {
				// source section is missing or unusable
				rebuildSections |= database_t::ALLOCMASK_HINTINDEX;
				inheritSections &= ~rebuildSections;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_HINTINDEX;
			}
		}

		/*
		 * imprint
		 */

		// interleave is not a section but a setting
		if (this->opt_interleave) {
			// user specified
			store.interleave = this->opt_interleave;
		} else if (db.interleave) {
			// inherit interleave
			store.interleave = db.interleave;
		} else {
			// set interleave on first time
			store.interleave = METRICS_DEFAULT_INTERLEAVE;
		}

		if (store.interleave) {
			const metricsInterleave_t *pMetrics = getMetricsInterleave(MAXSLOTS, store.interleave);
			if (!pMetrics)
				ctx.fatal("no preset for --interleave\n");

			store.interleave = pMetrics->numStored;
			store.interleaveStep = pMetrics->interleaveStep;
		}
		if (store.interleave != db.interleave) {
			// change of interleave triggers a rebuild (implicit disables inherit)
			rebuildSections |= database_t::ALLOCMASK_IMPRINT;
			inheritSections &= ~rebuildSections;
		}

		// data
		if (!store.maxSignature) {
			// no data to index
			store.interleave = 0;
			store.maxImprint = 0;
		} else {
			if (this->opt_maxImprint) {
				// user specified
				store.maxImprint = this->opt_maxImprint;
			} else if (inheritSections & database_t::ALLOCMASK_IMPRINT) {
				// inherited. pass-though
				store.maxImprint = db.numImprint;
			} else if (!readOnlyMode) {
				// resize using metrics
				const metricsImprint_t *pMetrics = getMetricsImprint(MAXSLOTS, ctx.flags & ctx.MAGICMASK_PURE, store.interleave, numNodes);
				if (!pMetrics)
					ctx.fatal("no preset for --maximprint\n");

				store.maxImprint = ctx.raisePercent(pMetrics->numImprint, 5);
			} else if (db.numImprint) {
				// non-empty. pass-though
				store.maxImprint = db.numImprint;
			} else {
				// empty. create minimal sized section
				store.interleave = 1;
				store.interleaveStep = MAXTRANSFORM;
				store.maxImprint = 1;
			}

			// imprint as data
			if (store.maxImprint > db.numImprint) {
				// disable inherit when section wants to grow
				inheritSections &= ~database_t::ALLOCMASK_IMPRINT;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_IMPRINT;
			}

			// imprint as index
			if (!db.numImprint || store.interleave != db.interleave) {
				// source section is missing or unusable
				rebuildSections |= database_t::ALLOCMASK_IMPRINT;
				inheritSections &= ~rebuildSections;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_IMPRINT;
			}
		}

		// index
		if (!store.maxImprint) {
			// no data to index
			store.imprintIndexSize = 0;
		} else {
			if (this->opt_imprintIndexSize) {
				// user specified
				store.imprintIndexSize = this->opt_imprintIndexSize;
			} else if (inheritSections & database_t::ALLOCMASK_IMPRINTINDEX) {
				// inherited. pass-though
				store.imprintIndexSize = db.imprintIndexSize;
			} else if (!readOnlyMode) {
				// auto-resize
				store.imprintIndexSize = ctx.nextPrime(store.maxImprint * this->opt_ratio);
			} else if (db.imprintIndexSize) {
				// non-empty. pass-though
				store.imprintIndexSize = db.imprintIndexSize;
			} else {
				// empty. create minimal sized section
				store.imprintIndexSize = 1;
			}

			if (store.imprintIndexSize != db.imprintIndexSize) {
				// source section is missing or unusable
				rebuildSections |= database_t::ALLOCMASK_IMPRINTINDEX;
				inheritSections &= ~rebuildSections;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_IMPRINTINDEX;
			}
		}

		/*
		 * member
		 */

		// data
		if (this->opt_maxMember) {
			// user specified
			store.maxMember = this->opt_maxMember;
		} else if (inheritSections & database_t::ALLOCMASK_MEMBER) {
			// inherited. pass-though
			store.maxMember = db.numMember;
		} else if (!readOnlyMode) {
			// resize using metrics
			const metricsGenerator_t *pMetrics = getMetricsGenerator(MAXSLOTS, ctx.flags & ctx.MAGICMASK_PURE, numNodes);
			if (!pMetrics)
				ctx.fatal("no preset for --maxmember\n");

			// give metrics a margin of error
			store.maxMember = ctx.raisePercent(pMetrics->numMember, 5);
		} else if (db.numMember) {
			// non-empty. pass-though
			store.maxMember = db.numMember;
		} else {
			// empty. create minimal sized section
			store.maxMember = 1;
		}

		if (store.maxMember > db.numMember) {
			// disable inherit when section wants to grow
			inheritSections &= ~database_t::ALLOCMASK_MEMBER;
		} else if (this->copyOnWrite) {
			// inherit when section fits and copy-on-write
			inheritSections |= database_t::ALLOCMASK_MEMBER;
		}

		// index
		if (!store.maxMember) {
			// no data to index
			store.memberIndexSize = 0;
		} else {
			if (this->opt_memberIndexSize) {
				// user specified
				store.memberIndexSize = this->opt_memberIndexSize;
			} else if (inheritSections & database_t::ALLOCMASK_MEMBERINDEX) {
				// inherited. pass-though
				store.memberIndexSize = db.memberIndexSize;
			} else if (!readOnlyMode) {
				// auto-resize
				store.memberIndexSize = ctx.nextPrime(store.maxMember * this->opt_ratio);
			} else if (db.memberIndexSize) {
				// non-empty. pass-though
				store.memberIndexSize = db.memberIndexSize;
			} else {
				// empty. create minimal sized section
				store.memberIndexSize = 1;
			}

			if (store.memberIndexSize != db.memberIndexSize) {
				// source section is missing or unusable
				rebuildSections |= database_t::ALLOCMASK_MEMBERINDEX;
				inheritSections &= ~rebuildSections;
			} else if (this->copyOnWrite) {
				// inherit when section fits and copy-on-write
				inheritSections |= database_t::ALLOCMASK_MEMBERINDEX;
			}
		}

		// rebuilt sections cannot be inherited
		inheritSections &= ~rebuildSections;

		if (ctx.opt_verbose >= ctx.VERBOSE_VERBOSE)
			fprintf(stderr, "[%s] Store create: maxSignature=%u signatureIndexSize=%u  maxHint=%u hintIndexSize=%u  interleave=%u maxImprint=%u imprintIndexSize=%u  maxMember=%u memberIndexSize=%u\n",
			        ctx.timeAsString(), store.maxSignature, store.signatureIndexSize, store.maxHint, store.hintIndexSize, store.interleave, store.maxImprint, store.imprintIndexSize, store.maxMember, store.memberIndexSize);

		// output data must be large enough to fit input data
		if (store.maxSignature < db.numSignature)
			ctx.fatal("--maxsignature=%u needs to be at least %u\n", store.maxSignature, db.numSignature);
		if (store.maxHint < db.numHint)
			ctx.fatal("--maxhint=%u needs to be at least %u\n", store.maxHint, db.numHint);
		if (store.maxMember < db.numMember)
			ctx.fatal("--maxmember=%u needs to be at least %u\n", store.maxMember, db.numMember);
	}

	/**
	 * @date 2020-04-27 20:08:14
	 *
	 * With copy-on-write, only `::memcpy()` when the output section if larger, otherwise inherit
	 *
	 * @date 2020-04-29 10:10:18
	 *
	 * Depending on the mmap() mode.
	 *
	 * It is still undecided to use:
	 *   `mmap(MAP_PRIVATE)` with advantage of copy-on-write but disadvantage that each process has a private copy of (many) page table entries.
	 *   `mmap(MAP_SHARED)` with advantage of shared PTE's but slow `::memcpy()` to private memory.
	 *
	 * Or it could be hybrid that many workers use `MAP_SHARED` and single process use `MAP_PRIVATE`.
	 *
	 * @param {database_t} store - writable output database
	 * @param {database_t} db - read-only input database
	 */
	void __attribute__((optimize("O0"))) populateDatabaseSections(database_t &store, const database_t &db) {

		if (ctx.opt_verbose >= ctx.VERBOSE_VERBOSE) {
			static char inheritText[512], rebuildText[512];
			store.sectionToText(inheritSections, inheritText);
			store.sectionToText(rebuildSections, rebuildText);
			fprintf(stderr, "[%s] copyOnWrite=%u inheritSections=[%s] rebuildSections=[%s]\n", ctx.timeAsString(), copyOnWrite, inheritText, rebuildText);
		}

		/*
		 * transforms are never invalid or resized
		 */

		if (inheritSections & database_t::ALLOCMASK_TRANSFORM) {
			assert(~store.allocFlags & database_t::ALLOCMASK_TRANSFORM);

			assert(db.numTransform == MAXTRANSFORM);
			store.maxTransform = db.numTransform;
			store.numTransform = db.numTransform;

			store.fwdTransformData = db.fwdTransformData;
			store.revTransformData = db.revTransformData;
			store.fwdTransformNames = db.fwdTransformNames;
			store.revTransformNames = db.revTransformNames;
			store.revTransformIds = db.revTransformIds;

			assert(db.transformIndexSize > 0);
			store.transformIndexSize = db.transformIndexSize;

			store.fwdTransformNameIndex = db.fwdTransformNameIndex;
			store.revTransformNameIndex = db.revTransformNameIndex;
		} else {
			assert(0);
		}

		/*
		 * signatures
		 */

		if (!store.maxSignature) {
			// set signatures to null but keep index intact for (empty) lookups
			store.signatures = NULL;
		} else {
			if (inheritSections & database_t::ALLOCMASK_SIGNATURE) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_SIGNATURE);
				store.signatures = db.signatures;
				store.numSignature = db.numSignature;
			} else if (!db.numSignature) {
				// input empty
				assert(store.allocFlags & database_t::ALLOCMASK_SIGNATURE);
				store.numSignature = 1;
			} else if (store.maxSignature <= db.numSignature && copyOnWrite) {
				// small enough to use copy-on-write
				assert(~store.allocFlags & database_t::ALLOCMASK_SIGNATURE);
				store.signatures = db.signatures;
				store.numSignature = db.numSignature;
			} else if (~rebuildSections & database_t::ALLOCMASK_SIGNATURE) {
				fprintf(stderr, "[%s] Copying signature section\n", ctx.timeAsString());

				assert(store.maxSignature >= db.numSignature);
				assert(store.allocFlags & database_t::ALLOCMASK_SIGNATURE);
				store.numSignature = db.numSignature;
				::memcpy(store.signatures, db.signatures, store.numSignature * sizeof(*store.signatures));
			}

			if (inheritSections & database_t::ALLOCMASK_SIGNATUREINDEX) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_SIGNATUREINDEX);
				store.signatureIndexSize = db.signatureIndexSize;
				store.signatureIndex = db.signatureIndex;
			} else if (rebuildSections & database_t::ALLOCMASK_SIGNATUREINDEX) {
				// post-processing
				assert(store.allocFlags & database_t::ALLOCMASK_SIGNATUREINDEX);
			} else if (!db.signatureIndexSize) {
				// was missing
				assert(store.allocFlags & database_t::ALLOCMASK_SIGNATUREINDEX);
				::memset(store.signatureIndex, 0, store.signatureIndexSize * sizeof(*store.signatureIndex));
			} else if (copyOnWrite) {
				// copy-on-write
				assert(store.signatureIndexSize == db.signatureIndexSize);
				assert(~store.allocFlags & database_t::ALLOCMASK_SIGNATUREINDEX);
				store.signatureIndex = db.signatureIndex;
				store.signatureIndexSize = db.signatureIndexSize;
			} else {
				// copy
				assert(store.signatureIndexSize == db.signatureIndexSize);
				assert(store.allocFlags & database_t::ALLOCMASK_SIGNATUREINDEX);
				store.signatureIndexSize = db.signatureIndexSize;
				::memcpy(store.signatureIndex, db.signatureIndex, store.signatureIndexSize * sizeof(*store.signatureIndex));
			}
		}

		/*
		 * hints
		 */

		if (!store.maxHint) {
			// set signatures to null but keep index intact for (empty) lookups
			store.hints = NULL;
		} else {
			if (inheritSections & database_t::ALLOCMASK_HINT) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_HINT);
				store.hints = db.hints;
				store.numHint = db.numHint;
			} else if (!db.numHint) {
				// input empty
				assert(store.allocFlags & database_t::ALLOCMASK_HINT);
				store.numHint = 1;
			} else if (store.maxHint <= db.numHint && copyOnWrite) {
				// small enough to use copy-on-write
				assert(~store.allocFlags & database_t::ALLOCMASK_HINT);
				store.hints = db.hints;
				store.numHint = db.numHint;
			} else if (~rebuildSections & database_t::ALLOCMASK_HINT) {
				fprintf(stderr, "[%s] Copying hint section\n", ctx.timeAsString());

				assert(store.maxHint >= db.numHint);
				assert(store.allocFlags & database_t::ALLOCMASK_HINT);
				store.numHint = db.numHint;
				::memcpy(store.hints, db.hints, store.numHint * sizeof(*store.hints));
			}

			if (inheritSections & database_t::ALLOCMASK_HINTINDEX) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_HINTINDEX);
				store.hintIndexSize = db.hintIndexSize;
				store.hintIndex = db.hintIndex;
			} else if (rebuildSections & database_t::ALLOCMASK_HINTINDEX) {
				// post-processing
				assert(store.allocFlags & database_t::ALLOCMASK_HINTINDEX);
			} else if (!db.hintIndexSize) {
				// was missing
				assert(store.allocFlags & database_t::ALLOCMASK_HINTINDEX);
				::memset(store.hintIndex, 0, store.hintIndexSize * sizeof(*store.hintIndex));
			} else if (copyOnWrite) {
				// copy-on-write
				assert(store.hintIndexSize == db.hintIndexSize);
				assert(~store.allocFlags & database_t::ALLOCMASK_HINTINDEX);
				store.hintIndex = db.hintIndex;
				store.hintIndexSize = db.hintIndexSize;
			} else {
				// copy
				assert(store.hintIndexSize == db.hintIndexSize);
				assert(store.allocFlags & database_t::ALLOCMASK_HINTINDEX);
				store.hintIndexSize = db.hintIndexSize;
				::memcpy(store.hintIndex, db.hintIndex, store.hintIndexSize * sizeof(*store.hintIndex));
			}
		}

		/*
		 * imprints
		 */

		if (!store.maxImprint) {
			// set signatures to null but keep index intact for (empty) lookups
			store.imprints = NULL;
		} else {
			if (inheritSections & database_t::ALLOCMASK_IMPRINT) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_IMPRINT);
				store.imprints = db.imprints;
				store.numImprint = db.numImprint;
			} else if (!db.numImprint) {
				// input empty
				assert(store.allocFlags & database_t::ALLOCMASK_IMPRINT);
				store.numImprint = 1;
			} else if (store.maxImprint <= db.numImprint && copyOnWrite) {
				// small enough to use copy-on-write
				assert(~store.allocFlags & database_t::ALLOCMASK_IMPRINT);
				store.imprints = db.imprints;
				store.numImprint = db.numImprint;
			} else if (~rebuildSections & database_t::ALLOCMASK_IMPRINT) {
				fprintf(stderr, "[%s] Copying imprint section\n", ctx.timeAsString());

				assert(store.maxImprint >= db.numImprint);
				assert(store.allocFlags & database_t::ALLOCMASK_IMPRINT);
				store.numImprint = db.numImprint;
				::memcpy(store.imprints, db.imprints, store.numImprint * sizeof(*store.imprints));
			}

			if (inheritSections & database_t::ALLOCMASK_IMPRINTINDEX) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_IMPRINTINDEX);
				store.imprintIndexSize = db.imprintIndexSize;
				store.imprintIndex = db.imprintIndex;
			} else if (rebuildSections & database_t::ALLOCMASK_IMPRINTINDEX) {
				// post-processing
				assert(store.allocFlags & database_t::ALLOCMASK_IMPRINTINDEX);
			} else if (!db.imprintIndexSize) {
				// was missing
				assert(store.allocFlags & database_t::ALLOCMASK_IMPRINTINDEX);
				::memset(store.imprintIndex, 0, store.imprintIndexSize * sizeof(*store.imprintIndex));
			} else if (copyOnWrite) {
				// copy-on-write
				assert(store.imprintIndexSize == db.imprintIndexSize);
				assert(~store.allocFlags & database_t::ALLOCMASK_IMPRINTINDEX);
				store.imprintIndex = db.imprintIndex;
				store.imprintIndexSize = db.imprintIndexSize;
			} else {
				// copy
				assert(store.imprintIndexSize == db.imprintIndexSize);
				assert(store.allocFlags & database_t::ALLOCMASK_IMPRINTINDEX);
				store.imprintIndexSize = db.imprintIndexSize;
				::memcpy(store.imprintIndex, db.imprintIndex, store.imprintIndexSize * sizeof(*store.imprintIndex));
			}
		}

		/*
		 * members
		 */

		if (!store.maxMember) {
			// set signatures to null but keep index intact for (empty) lookups
			store.members = NULL;
		} else {
			if (inheritSections & database_t::ALLOCMASK_MEMBER) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_MEMBER);
				store.members = db.members;
				store.numMember = db.numMember;
			} else if (!db.numMember) {
				// input empty
				assert(store.allocFlags & database_t::ALLOCMASK_MEMBER);
				store.numMember = 1;
			} else if (store.maxMember <= db.numMember && copyOnWrite) {
				// small enough to use copy-on-write
				assert(~store.allocFlags & database_t::ALLOCMASK_MEMBER);
				store.members = db.members;
				store.numMember = db.numMember;
			} else if (~rebuildSections & database_t::ALLOCMASK_MEMBER) {
				fprintf(stderr, "[%s] Copying member section\n", ctx.timeAsString());

				assert(store.maxMember >= db.numMember);
				assert(store.allocFlags & database_t::ALLOCMASK_MEMBER);
				store.numMember = db.numMember;
				::memcpy(store.members, db.members, store.numMember * sizeof(*store.members));
			}

			if (inheritSections & database_t::ALLOCMASK_MEMBERINDEX) {
				// inherited. pass-though
				assert(~store.allocFlags & database_t::ALLOCMASK_MEMBERINDEX);
				store.memberIndexSize = db.memberIndexSize;
				store.memberIndex = db.memberIndex;
			} else if (rebuildSections & database_t::ALLOCMASK_MEMBERINDEX) {
				// post-processing
				assert(store.allocFlags & database_t::ALLOCMASK_MEMBERINDEX);
			} else if (!db.memberIndexSize) {
				// was missing
				assert(store.allocFlags & database_t::ALLOCMASK_MEMBERINDEX);
				::memset(store.memberIndex, 0, store.memberIndexSize * sizeof(*store.memberIndex));
			} else if (copyOnWrite) {
				// copy-on-write
				assert(store.memberIndexSize == db.memberIndexSize);
				assert(~store.allocFlags & database_t::ALLOCMASK_MEMBERINDEX);
				store.memberIndex = db.memberIndex;
				store.memberIndexSize = db.memberIndexSize;
			} else {
				// copy
				assert(store.memberIndexSize == db.memberIndexSize);
				assert(store.allocFlags & database_t::ALLOCMASK_MEMBERINDEX);
				store.memberIndexSize = db.memberIndexSize;
				::memcpy(store.memberIndex, db.memberIndex, store.memberIndexSize * sizeof(*store.memberIndex));
			}
		}
	}
};

#endif