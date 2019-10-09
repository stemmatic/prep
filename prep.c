
/*
	prep - Prepare a collation into a form _stemma_ can use.

	Usage: prep collation {taxa}*

	In files:
		collation - collation information for the MSS

	Out files:
		matrix    - the matrix of taxa and variants (*.tx)
		strat     - stratigraphical constraints     (*.no)
		variants  - listing of each variant         (*.vr)

	Environmental controls:
		YEARGRAN  - Granularity for years.  Default is (-1),
					which is tuned for the N.T.
		FTHRESH   - Threshold number of non-constant variants
					for including fragmentary witnesses.  (250)
		CTHRESH   - Threshold number of new non-constant variants
					for including corrected witnesses.  (100)
		YEAR      - Cut off year for witnesses.
		NOSING    - No singular readings in matrix.
		ROOT      - Define an explicit root/ancestor (e.g. UBS).

	Special macros:
		$*		  - All witnesses
		$?		  - Witnesses with unknown readings
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include "prep.h"

/* Tokens:
	*	- names of MSS
	;	- list terminator for *, =, etc.
	/	- switch parallel
	"   - file comment
	=	- define macro
	^	- chronological file
	~	- Alias name (useful for Hoskier, etc.)
	@	- Verse maker
	{	- Bracket section of readings and witnesses (used for vi filter)
	}	- End Section
	[	- Begin readings
	|	- separator  |*{weight}
	]	- End readings
	<	- Begin witnesses
	|	- separator
	>	- End Witness
	$	- macro
	-	- suppress witness
	:	- corrector
	!	- user end
	default - name of witness/reading
*/

#define NOMSS (-1)
#define SUPPRESSED (-2)
#define BADHAND (-3)

#define CVT '~'					// Convert separator

#define MISSING  '?'
#define LACUNOSE '.'
#define UNASSIGN ':'

#define YEARGRAN 100			// Assume 100-year granularity
#define LITGRAN (-1)			// Literary Granularity, use table
static int YearGran = LITGRAN;

#define FTHRESHOLD 250
#define CTHRESHOLD 100
#define WEIGHBYED 6

typedef enum { OK, WARN, END, FATAL, } Status;

#define EAT(ctx, t, u)     while ((t = getToken(ctx)) && *t != u)

static char *getToken(Context *ctx);
static Macro *getMacro(Context *ctx, char *token);
static int findMSS(Context *ctx, char *name, int *hand);
static int findPar(Context *ctx, int code);
static char *parName(Context *ctx, int pp, Testim *t, int h, char *name);
static char *append(char *src, char *end, char *dst);
static void fWarn(Context *ctx, char *cmd, char *msg, char *arg);
static int activeMSS(Context *ctx);
#define EOFWARN(ctx, cmd) fWarn(ctx, cmd, "Unexpected end of file", "")

static void writeTx(Context *ctx);
static void writeNo(Context *ctx);
static void writeVr(Context *ctx);
static void mandateTx(Context *ctx);
static void suppressTx(Context *ctx);
static void suppressVr(Context *ctx);
static void suppressId(Context *ctx);

static int initContext(Context *ctx, int argc, char *argv[]);
static Status doMSS(Context *ctx);
static Status doParallel(Context *ctx);
static Status doDefine(Context *ctx);
static Status doVerse(Context *ctx);
static Status doReadings(Context *ctx);
static Status doWitnesses(Context *ctx);
static Status doChron(Context *ctx);
static Status doSuppress(Context *ctx);
static Status doComment(Context *ctx);
static Status doAlias(Context *ctx);

static Status vrVerse(Context *ctx);
static Status vrReadings(Context *ctx);

static int litStratum(int year);

int
	main(int argc, char *argv[])
{
	Status status = OK;
	char *token;
	static Context ctx[1];
	int nWarn = 0;
	char *yearGran;

	if (!initContext(ctx, argc, argv))
		return -2;

	if ((yearGran = getenv("YEARGRAN")))
		YearGran = atoi(yearGran);

	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			fWarn(ctx, "?", "Unknown token:", token);
			status = WARN;
			break;
		case '!':		// User requested end
			status = END;
			break;
		case '*':
			status = doMSS(ctx);
			break;
		case '/':
			status = doParallel(ctx);
			break;
		case '=':
			status = doDefine(ctx);
			break;
		case '@':
			status = doVerse(ctx);
			break;
		case '[':
			status = doReadings(ctx);
			break;
		case '<':
			status = doWitnesses(ctx);
			break;
		case '~':
			status = doAlias(ctx);
			break;
		case '^':
			status = doChron(ctx);
			break;
		case '-':
			status = doSuppress(ctx);
			break;
		case '"':
			status = doComment(ctx);
			break;
		case '{':
		case '}':
			// Ignore
			status = OK;
			break;
		}
		if (status == END || status == FATAL)
			break;
		if (status == WARN)
			nWarn++;
	}

	if (status == FATAL)
		fprintf(stderr, "Fatal error, terminating ...\n");
	else if (nWarn == 0) {
		mandateTx(ctx);
		suppressVr(ctx);
		suppressTx(ctx);
		suppressVr(ctx);
		if (!getenv("IDOK"))
			suppressId(ctx);
		writeTx(ctx);
		writeNo(ctx);
		writeVr(ctx);
	} else
		fprintf(stderr, "Too many warnings, terminating ...\n");

	fclose(ctx->fpVr);
	fclose(ctx->fpTx);
	fclose(ctx->fpMss);
	return (status != OK && status != END) ? -status : nWarn;
}

static char *
	getToken(Context *ctx)
{
	char *s;
	int ch;

	if (ctx->inc_line_p) {
		ctx->lineno++;
		ctx->inc_line_p = NO;
	}

	// Skip initial white space
	while ((ch = fgetc(ctx->fpMss)) != EOF && isspace(ch)) {
		if (ch == '\n')
			ctx->lineno++;
		continue;
	}
	if (ch == EOF)
		return (char *) 0;

	// Collect chars for token
	s = ctx->token;
	do {
		if (s == &ctx->token[MAXTOKEN-1]) {
			fprintf(stderr, "WARN: Max token size (%d) exceeded: %s\n",
				MAXTOKEN, ctx->token);
			break;
		}
		*s++ = ch;
	} while ((ch = fgetc(ctx->fpMss)) != EOF && !isspace(ch));

	if (ch == '\n')
		ctx->inc_line_p = YES;

	*s = EOS;
	return ctx->token;
}

static Macro *
	getMacro(Context *ctx, char *token)
{
	int name = (int) token[1];

	if (name < 0 || name > MAXMACRO) {
		fWarn(ctx, "<", "Out-of-range macro (could be Greek):", token);
		return 0;
	}
	return ctx->par[ctx->parallel].pMacros[name];
}

static int
	findMSS(Context *ctx, char *name, int *hand)
{
	int ms, status;
	char *dot, *colon;
	dot = strchr(name, '.');
	colon = strchr(name, ':');

	// ECM data uses the dot as a witness separator, so chuck the trailing dot.
	if (dot && dot[1] == EOS)
		*dot = EOS;

	*hand = 0;
	if (*name == '-')
		return SUPPRESSED;

	if (colon) {
		*hand = atoi(colon+1);
		if (*hand > MAXHAND)
			return BADHAND;
		*colon = EOS;
	}

	status = NOMSS;
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[ctx->parallel].testim[ms];
		if (strcmp(name, w->name) == 0) {
			if (t->hands[*hand].suppressed)
				status = SUPPRESSED;
			else if (ctx->Root && ms == 0)
				status = NOMSS;				// Cannot find ROOT
			else
				status = ms;
			break;
		}
	}

	if (colon)
		*colon = ':';
	return status;
}

static int
	findPar(Context *ctx, int code)
{
	int pp;

	for (pp = 0; pp < ctx->nParallels; pp++) {
		Parallel *p = &ctx->par[pp];
		if (p->name_space == code)
			return pp;
	}
	return pp;
}

static char *
	parName(Context *ctx, int pp, Testim *t, int h, char *name)
{
	int code;
	static char buf[MAXTOKEN];
	char *b = buf;

	b += sprintf(buf, "%s", name);
	if (t && t->corrected)
		b += sprintf(b, ":%d", h);

	code = ctx->par[pp].name_space;
	if (code > 1)
		sprintf(b, "/%c", code);

	return buf;
}

static char *
	append(char *src, char *end, char *dst)
{
	if (src == end)
		return src;
	do {
		if (src+1 == end) {
			*src = EOS;
			return src;
		}
	} while ((*src++ = *dst++));
	return src-1;
}

static void
	fWarn(Context *ctx, char *cmd, char *msg, char *arg)
{
	int col;

	col = fprintf(stderr, "%4lu: %s", ctx->lineno, cmd);
	do { col += fprintf(stderr, " "); } while (col < 6);

	col += fprintf(stderr, "%s", msg);
	if (*arg)
		col += fprintf(stderr, " %s", arg);
	do { col += fprintf(stderr, " "); } while (col < 31);

	col += fprintf(stderr, "@ %s", ctx->par[ctx->parallel].position);
	do { col += fprintf(stderr, " "); } while (col < 50);

	if (*ctx->lemma != EOS)
		fprintf(stderr, "[ %s ]", ctx->lemma);

	fprintf(stderr, "\n");
}

// activeMSS() - return the number of active (non-suppressed) witnesses

static int
	activeMSS(Context *ctx)
{
	int pp, ms, hand;
	int nActive = 0;

	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		Testim *t = &ctx->par[pp].testim[ms];
		for (hand = 0; hand < MAXHAND; hand++) {
			if (!t->hands[hand].suppressed)
				nActive++;
		}
	}
	return nActive;
}


static int
	findAland(Context *ctx, char *name, int *hand, int ms)
{
	char *colon = strchr(name, ':');

	if (ms >= ctx->nMSS)
		return ms;

	if (colon) {
		*hand = atoi(colon+1);
		*colon = EOS;
	} else
		*hand = 0;

	do {
		if (strcmp(name, ctx->mss[ms].Aland) == 0)
			break;
	} while (++ms < ctx->nMSS);

	if (colon)
		*colon = ':';

	return ms;
}

// Mandate a command-line selected subset of witnesses

static void
	mandateTx(Context *ctx)
{
	Parallel *para = &ctx->par[ctx->parallel];
	Testim *t;
	Macro *macro;
	char **mandatees;
	int pp, ms, hand;
	Status status = OK;

	// Nothing specified on the command-line
	assert( ctx->subset );
	if (!*ctx->subset)
		return;

	for (mandatees = ctx->subset; *mandatees; mandatees++) {
		char *mand = *mandatees;

		switch (mand[0]) {
		default:
			ms = findMSS(ctx, mand, &hand);
			if (ms == SUPPRESSED) {
				fWarn(ctx, "+", "Already suppressed:", mand);
				status = WARN;
				continue;
			}
			if (ms == NOMSS || ms == BADHAND) {
				fWarn(ctx, "+", "Unknown:", mand);
				status = WARN;
				continue;
			}
			t = &para->testim[ms];
			t->hands[hand].mandated = YES;
			break;
		case '$':
			macro = getMacro(ctx, mand);
			if (!macro) {
				fWarn(ctx, "+", "Unknown macro:", mand);
				status = WARN;
				continue;
			}
			for (ms = (ctx->Root) ? 1 : 0; ms < ctx->nMSS; ms++) {
				if (!macro->inset[ms])
					continue;
				t = &para->testim[ms];
				t->hands[0].mandated = YES;
			}
			break;
		}
	}

	if (status != OK)
		return;

	// Suppress everything not mandated.
	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		t = &ctx->par[pp].testim[ms];
		for (hand = 0; hand < MAXHAND; hand++) {
			if (!t->hands[hand].suppressed && !t->hands[hand].mandated)
				t->hands[hand].suppressed = YES;
		}
	}
}

static void
	suppressTx(Context *ctx)
{
	int fThresh, cThresh;
	int ms, pc, h, i, nExtant, nCorrs;
	int pp;
	int var;
	char * r;
	char *threshenv, *yearenv;
	int year, lastHand;

	fThresh = (threshenv = getenv("FTHRESH")) ? atoi(threshenv)
//		: (ctx->nVar > 2*FTHRESHOLD) ? FTHRESHOLD
		: ctx->wvar/2 + 1;
	cThresh = (threshenv = getenv("CTHRESH")) ? atoi(threshenv)
		: (ctx->nVar > 2*CTHRESHOLD) ? CTHRESHOLD 
		: ctx->wvar/10 + 1;
	fprintf(stderr, "Thresholds: frag=%d, corr=%d; adjustments:",
		fThresh, cThresh);
	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[pp].testim[ms];

		if (t->hands[0].suppressed)
			continue;
		t->corrected = NO;
		nExtant = 0;
		var = 0;
		for (pc = 0; pc < ctx->nPiece; pc++) {
			if (!t->hands[0].sets[pc]) {
				var += ctx->pieceUnits[pc];
				continue;
			}
			r = t->hands[0].sets[pc];
			for (i = 0; r[i]; i++) {
				if (r[i] != MISSING)
					nExtant += ctx->wgts[var];
				var++;
			}
		}
		if ((!ctx->Root || ms > 0) && (nExtant < fThresh) && !t->hands[0].mandated) {
			t->hands[0].suppressed = YES;
			fprintf(stderr, " -%s(%d)",
				parName(ctx, pp, t, 0, w->name), nExtant);
		}

		lastHand = 0;
		for (h = 1; h < MAXHAND; h++) {
			nCorrs = 0;
			var = 0;
			for (pc = 0; pc < ctx->nPiece; pc++) {
				char *lh;
				if (!t->hands[h].sets[pc]) {
					t->hands[h].sets[pc] = t->hands[lastHand].sets[pc];
					var += ctx->pieceUnits[pc];
					continue;
				}
				r = t->hands[h].sets[pc];
				lh = t->hands[lastHand].sets[pc];
				for (i = 0; r[i]; i++) {
					if (lh && r[i] != lh[i])
						nCorrs += ctx->wgts[var];
					var++;
				}
			}
			if ((nCorrs < cThresh) && !t->hands[h].mandated ) {
				t->hands[h].suppressed = YES;
				if (nCorrs > cThresh/2) {
					t->corrected = YES;
					fprintf(stderr, " -%s(%d)",
						parName(ctx, pp, t, h, w->name), nCorrs);
					t->corrected = NO;
				}
			} else {
				t->corrected = YES;
				t->hands[h].suppressed = NO;
				t->hands[h].lastHand = lastHand;
				lastHand = h;
				fprintf(stderr, " +%s(%d)",
					parName(ctx, pp, t, h, w->name), nCorrs);
			}
		}
	}
	fprintf(stderr, "\n");
	
	// Suppress by year
	if ((yearenv = getenv("YEAR")) == 0)
		return;
	year = atoi(yearenv);
	
	fprintf(stderr, "Year suppression at %d:", year);
	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[pp].testim[ms];

		for (h = 0; h < MAXHAND; h++) {
			if (t->hands[h].suppressed)
				continue;
			if ((t->hands[h].earliest > year) && !t->hands[h].mandated) {
				t->hands[h].suppressed = YES;
				fprintf(stderr, " -%s(%d)",
					parName(ctx, pp, t, h, w->name), t->hands[h].earliest);
			}
		}
	}
	fprintf(stderr, "\n");
}

// Suppress constant variants

static void
	suppressVr(Context *ctx)
{
	int ms, pp, h, pc, pv, var;
	char *r;
	int state;

	var = 0;
	for (pc = 0; pc < ctx->nPiece; pc++) {
		for (pv = 0; pv < ctx->pieceUnits[pc]; pv++) {
			static unsigned char states[256];	// States attested among kids
			static int count[256];			// Count of corresponding state
			int ss, nStates=0; 				// Number of different states
			int dblCount=0;					// Count for the twice attested

			for (pp = 0; pp < ctx->nParallels; pp++)
			for (ms = 0; ms < ctx->nMSS; ms++) {
				register Testim  *t = &ctx->par[pp].testim[ms];
				int defchar = (ctx->Root && pp == 00 && ms == 0)
					? '0' : MISSING;

				for (h = 0; h < MAXHAND; h++) {
					if (t->hands[h].suppressed)
						continue;
					r = t->hands[h].sets[pc];
					if (!r && h > 0)
						r = t->hands[t->hands[h].lastHand].sets[pc];
					state = (r) ? r[pv] : defchar;

					if (state == MISSING)
						continue;

					// Find state index, adjust count if found, else add one
					for (ss = 0; ss < nStates; ss++) {
						if (states[ss] == state)
							break;
					}
					if (ss < nStates) {
						count[ss]++;
						if (count[ss] == 2)
							dblCount++;
					} else {
						ss = nStates++;
						states[ss] = state;
						count[ss] = 1;
					}
				}
			}

			// Suppress this variation if we're same/constant.
			if (nStates <= 1
			|| (getenv("NOSING") && dblCount <= 1)) {
				ctx->wvar -= ctx->wgts[var];
				ctx->wgts[var] = 0;
			}
			var++;
		}
	}
}

// Suppress identical witnesses

static void
	suppressId(Context *ctx)
{
	int pp, ms;

	fprintf(stderr, "Checking identical witnesses:");

	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[pp].testim[ms];
		int m2;

		if (t->hands[0].suppressed)
			continue;
		for (m2 = 0; m2 < ms; m2++) {
			register Witness *w2 = &ctx->mss[m2];
			register Testim  *t2 = &ctx->par[pp].testim[m2];
			int pc;

			if (t2->hands[0].suppressed)
				continue;
			for (pc = 0; pc < ctx->nPiece; pc++) {
				if (t->hands[0].sets[pc] != t2->hands[0].sets[pc])
					break;
			}
			if (pc == ctx->nPiece) {
				t->hands[0].suppressed = YES;
				fprintf(stderr, " -%s=%s", w->name, w2->name);
				break;
			}
		}
	}
	fprintf(stderr, " Done\n");
}

static void
	writeTx(Context *ctx)
{
	int ms, pp, h, pc, pv, vv, var;
	int nActive = activeMSS(ctx);

	// Output
	printf("Year granularity: %d\n", YearGran);
	printf("Active witnesses: %d, weighted variants: %d\n", nActive, ctx->wvar);
	printf("Witnesses:");

	fprintf(ctx->fpTx, "%-9d %d\n", nActive, ctx->wvar);
	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[pp].testim[ms];
		int defchar = (ctx->Root && pp == 0 && ms == 0) ? '0' : MISSING;

		for (h = 0; h < MAXHAND; h++) {
			if (t->hands[h].suppressed)
				continue;
			fprintf(ctx->fpTx, "%-9s ", parName(ctx, pp, t, h, w->pname));
			printf(" %s", parName(ctx, pp, t, h, w->pname));
			var = 0;
			for (pc = 0; pc < ctx->nPiece; pc++) {
				char *r = t->hands[h].sets[pc];
				if (!r && h > 0)
					r = t->hands[t->hands[h].lastHand].sets[pc];
				for (pv = 0; pv < ctx->pieceUnits[pc]; pv++) {
					for (vv = 0; vv < ctx->wgts[var]; vv++)
						fputc((r) ? r[pv] : defchar, ctx->fpTx);
					var++;
				}
			}
			fprintf(ctx->fpTx, "\n");
		}
	}
	printf("\n");
}

static void
	stratify(Context *ctx)
{
#define MAXYEAR 2050
	static int strata[MAXYEAR];
	int stratum;
	int ms, pp, h, yr;	

	for (yr = 0; yr < MAXYEAR; yr++)
		strata[yr] = NO;

	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Testim  *t = &ctx->par[pp].testim[ms];

		for (h = 0; h < MAXHAND; h++) {
			if (t->hands[h].suppressed)
				continue;
			stratum = litStratum(t->hands[h].average);
			t->hands[h].stratum = stratum;
			strata[stratum] = YES;
		}
	}

	stratum = 0;
	for (yr = 0; yr < MAXYEAR; yr++) {
		if (strata[yr])
			strata[yr] = stratum++;
	}

	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Testim  *t = &ctx->par[pp].testim[ms];
		for (h = 0; h < MAXHAND; h++) {
			if (t->hands[h].suppressed)
				continue;
			t->hands[h].stratum = strata[t->hands[h].stratum];
		}
	}
}

static void
	writeNo(Context *ctx)
{
	int pp, p2;
	int ms, m2;
	int h, h2;

	// Output
	stratify(ctx);
	for (pp = 0; pp < ctx->nParallels; pp++)
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Witness *w = &ctx->mss[ms];
		register Testim  *t = &ctx->par[pp].testim[ms];
		for (h = 0; h < MAXHAND; h++) {
			if (t->hands[h].suppressed)
				continue;
			if (t->hands[h].latest == INT_MAX) {
				fprintf(stderr, "No chron entry for ");
				fprintf(stderr, "%s",      parName(ctx, pp, t, h, w->name));
				fprintf(stderr, " ~ %s",   parName(ctx, pp, 0, 0, w->Aland));
				fprintf(stderr, " ~ %s\n", parName(ctx, pp, 0, 0, w->pname));
			}
			fprintf(ctx->fpNo, "%-9s %d < ",
				parName(ctx, pp, t, h, w->pname), t->hands[h].stratum);

			for (p2 = 0; p2 < ctx->nParallels; p2++)
			for (m2 = 0; m2 < ctx->nMSS; m2++) {
				register Witness *w2 = &ctx->mss[m2];
				register Testim  *t2 = &ctx->par[p2].testim[m2];
				for (h2 = 0; h2 < MAXHAND; h2++) {
					if (t2->hands[h2].suppressed)
						continue;
					if (t->hands[h].earliest > t2->hands[h2].latest) {
						fprintf(ctx->fpNo, "%s ",
							parName(ctx, p2, t2, h2, w2->pname));
					} else if (w == w2 && pp == p2 && h >= h2) {
						fprintf(ctx->fpNo, "%s ",
							parName(ctx, p2, t2, h2, w2->pname));
					}
				}
			}
			fprintf(ctx->fpNo, ">\n");
		}
	}
}

static FILE *
	outFile(char *base, char *ext)
{
	char fn[MAXTOKEN];
	FILE *fp;

	sprintf(fn, "%s.%s", base, ext);

	fp = fopen(fn, "w");
	if (!fp)
		fprintf(stderr, "Cannot open: %s\n", fn);
	return fp;
}

static int
	initContext(Context *ctx, int argc, char *argv[])
{
	int ii;
	char *token;
	char *base;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s ms-coll {witness}*\n", argv[0]);
		return NO;
	}

	base = argv[1];
	ctx->subset = &argv[2];

	ctx->fpMss = fopen(base, "r");
	if (!ctx->fpMss) {
		fprintf(stderr, "Cannot open collation file: %s\n", base);
		return NO;
	}

	if (!(ctx->fpTx = outFile(base, "tx")))
		return NO;

	if (!(ctx->fpNo = outFile(base, "no")))
		return NO;

	if (!(ctx->fpVr = outFile(base, "vr")))
		return NO;

	ctx->Root = getenv("ROOT");
	ctx->nMSS = (ctx->Root) ? 1 : 0;
	ctx->nVar = 0;
	ctx->nPiece = 0;
	ctx->nSets = 0;

	ctx->nParallels = 0;

	ctx->lineno = 0;
	ctx->inc_line_p = YES;
	for (ii = 0; ii < dimof(ctx->par); ii++)
		strcpy(ctx->par[ii].position, "Beginning");
	ctx->lemma[0] = EOS;

	while ((token = getToken(ctx)) && *token != '!') {
		switch (*token) {
		default:
			break;
		case '@':
			token = getToken(ctx);
			if (!token) {
				EOFWARN(ctx, "@");
				return NO;
			}
			strcpy(ctx->par[ctx->parallel].position, token);
			break;
		case '*':
			while ((token = getToken(ctx)) && *token != ';') {
				if (*token == '"')
					EAT(ctx, token, '"');
				else if (*token == '/')
					ctx->par[ctx->nParallels++].name_space = token[1];
				else
					ctx->nMSS++;
			}
			if (!token) {
				EOFWARN(ctx, "*");
				return NO;
			}
			break;
		case '<':
			ctx->nSets++;
			while ((token = getToken(ctx)) && *token != '>') {
				if (*token == '"') {
					EAT(ctx, token, '"');
					if (!token) {
						EOFWARN(ctx, "\"");
						return NO;
					}
				}
				if (*token == '|')
					ctx->nSets++;
			}
			if (!token) {
				EOFWARN(ctx, "<");
				return NO;
			}
			break;
		case '[':
			ctx->nPiece++;
			while ((token = getToken(ctx)) && *token != ']') {
				if (*token == '"') {
					EAT(ctx, token, '"');
					if (!token) {
						EOFWARN(ctx, "\"");
						return NO;
					}
				}
				if (*token == '|')
					ctx->nVar++;
			}
			if (!token) {
				EOFWARN(ctx, "[");
				return NO;
			}
			break;
		case '"':
			EAT(ctx, token, '"');
			if (!token) {
				EOFWARN(ctx, "\"");
				return NO;
			}
			break;
		}
	}
	if (ctx->nParallels == 0) {
		ctx->nParallels = 1;
		ctx->par[0].name_space = EOS;
	}
	ctx->parallel = 0;

	rewind(ctx->fpMss);
	ctx->lineno = 0;
	ctx->inc_line_p = YES;
	for (ii = 0; ii < argc; ii++)
		printf("%s%c", argv[ii], (ii < argc-1) ? ' ' : '\n');
	printf("Parallels=%d; MSS=%d; VarUnits=%d; Pieces=%d; Sets=%d\n",
		ctx->nParallels, ctx->nMSS, ctx->nVar, ctx->nPiece, ctx->nSets);
	if (ctx->nMSS == 0) {
		fprintf(stderr, "No witnesses, terminating...\n");
		return NO;
	}	

	ctx->mss = (Witness *) 0;	// to be initialized in doMSS()

	strcpy(ctx->par[ctx->parallel].position, "Beginning");

	// Set up array of readings for each variation unit.
	ctx->nRdgs = new(ctx->nVar, int);
	assert( ctx->nRdgs );
	ctx->wgts = new(ctx->nVar, int);
	assert( ctx->wgts );
	for (ii = 0; ii < ctx->nVar; ii++) {
		ctx->nRdgs[ii] = 0;
		ctx->wgts[ii] = 1;
	}

	{
		char *w = getenv("WEIGHBYED");
		ctx->weighByED = (w) ? atoi(w) : WEIGHBYED;
	}
	
	// Set up states array
	ctx->states = new(ctx->nSets, char *);
	assert( ctx->states );
	for (ii = 0; ii < ctx->nSets; ii++)
		ctx->states[ii] = (char *) 0;

	// Set up pieceUnits count
	ctx->pieceUnits = new(ctx->nPiece, int);
	assert( ctx->pieceUnits );
	for (ii = 0; ii < ctx->nPiece; ii++)
		ctx->pieceUnits[ii] = 0;

	for (ii = 0; ii < ctx->nParallels; ii++) {
		int jj;

		ctx->par[ii].pMacros = new(MAXMACRO, Macro *);
		assert( ctx->par[ii].pMacros );
		for (jj = 0; jj < MAXMACRO; jj++)
			ctx->par[ii].pMacros[jj] = (Macro *) 0;
	}
	ctx->macLevel = 1;

	ctx->var = 0;
	ctx->wvar = 0;
	ctx->set = 0;
	ctx->piece = -1;
	return YES;
}

static void
	initWitness(Context *ctx, Witness *w, char *name)
{
	char *s;

	assert( w );
	w->name = strdup(name);
	w->Aland = w->name;
	w->pname = w->name;
	assert( w->name );

	// Inline aliasing
	if ((s = strchr(w->name, CVT))) {
		*s++ = EOS;
		w->Aland = strdup(s);

		if ((s = strchr(w->Aland, CVT))) {
			*s++ = EOS;
			w->pname = strdup(s);
		}
	}

}

static void
	initParallel(Context *ctx, Parallel *p, int name)
{
	int ms, h;
	Macro *all; 		// The $* macro, holds all the taxa
	Macro *miss;		// The $? macro, holds the missing taxa

	// One-char name
	p->name_space = name;

	// Set up macro for all.
	all = new(1, Macro);
	assert( all );

	all->level = ctx->macLevel++;
	all->inset = new(ctx->nMSS, int);
	assert( all->inset );
	p->pMacros['*'] = all;

	// Set up macro for missings.
	miss = new(1, Macro);
	assert( miss );

	miss->level = MAXMACRO-1;
	miss->inset = new(ctx->nMSS, int);
	assert( miss->inset );
	p->pMacros['?'] = miss;

	p->testim = new(ctx->nMSS, Testim);
	assert( p->testim );

	for (ms = 0; ms < ctx->nMSS; ms++) {
		Testim *t = &p->testim[ms];
		all->inset[ms] = YES;
		miss->inset[ms] = NO;

		t->hands = new(MAXHAND, Hand);
		for (h = 0; h < MAXHAND; h++) {
			int ii;

			t->hands[h].sets = new(ctx->nPiece, char *);
			assert( t->hands[h].sets );
			for (ii = 0; ii < ctx->nPiece; ii++)
				t->hands[h].sets[ii] = (char *) 0;
			t->hands[h].earliest = t->hands[h].average = 0;
			t->hands[h].latest = INT_MAX;
			t->hands[h].suppressed = (ctx->Root && ms == 0) ? YES : NO;		// Possibly redundant with code in doMSS()
			t->hands[h].mandated = NO;
		}
		t->corrected = NO;
	}
}

// Syntax: * {mss-names}+ ;
static Status
	doMSS(Context *ctx)
{
	int ms, pp;
	char *token;

	if (ctx->mss) {
		fWarn(ctx, "*", "Already declared the witnesses.", "");
		return FATAL;
	}
	ctx->mss = new(ctx->nMSS, Witness);
	assert( ctx->mss );
	
	pp = ms = 0;
	if (ctx->Root)
		initWitness(ctx, &ctx->mss[ms++], ctx->Root);
		
	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			initWitness(ctx, &ctx->mss[ms++], token);
			break;

		case '/':
			initParallel(ctx, &ctx->par[pp++], token[1]);
			break;

		case '"':
			EAT(ctx, token, '"');
			break;

		case ';':
			// Global Macro
			assert( ms == ctx->nMSS );
			if (pp == 0)
				initParallel(ctx, &ctx->par[0], EOS);

			// Unsuppress ROOT in the earliest parallel
			if (ctx->Root) {
				Hand *hand = &ctx->par[0].testim[0].hands[0];
				hand->earliest = hand->average = hand->latest = 0;
				hand->suppressed = NO;
				hand->mandated = YES;
			}
	
			return OK;
		}
	}
	EOFWARN(ctx, "*");
	assert( token );
	return FATAL;
}

// Syntax: /a   " in which 'a' is the name space
static Status
	doParallel(Context *ctx)
{
	assert( ctx->token[0] == '/' );

	ctx->parallel = findPar(ctx, (int) ctx->token[1]);
	if (ctx->parallel == ctx->nParallels) {
		fWarn(ctx, "/", "Unknown parallel:", ctx->token);
		return FATAL;
	}
	return OK;
}

// Syntax: = ${macro-name} {mss-name}+ ;
static Status
	doDefine(Context *ctx)
{
	char *token;
	Macro *macro, *mac2;
	int name;
	int ms, hand;
	int add=NO, sub=NO;
	int nWarn = 0;

	if (ctx->token[1] == '+')
		add = YES;
	if (ctx->token[1] == '-')
		sub = YES;
	token = getToken(ctx);
	if (*token != '$') {
		fWarn(ctx, "=", "Macro name must begin with $:", token);
		return FATAL;
	}

	name = (int) token[1];
	if (name < 0 || name > MAXMACRO) {
		fWarn(ctx, "=", "Out-of-range macro (could be Greek):", token);
		return FATAL;
	}

	macro = ctx->par[ctx->parallel].pMacros[name];
	if (!macro) {
		macro = new(1, Macro);
		assert( macro );
		macro->level = ctx->macLevel++;
		macro->inset = new(ctx->nMSS, int);
		assert( macro->inset );
		ctx->par[ctx->parallel].pMacros[name] = macro;
		for (ms = 0; ms < ctx->nMSS; ms++)
			macro->inset[ms] = NO;
	} else if (!add && !sub) {
		for (ms = 0; ms < ctx->nMSS; ms++)
			macro->inset[ms] = NO;
	}

	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			ms = findMSS(ctx, token, &hand);
			if (ms == NOMSS) {
				fWarn(ctx, "=", "Unknown:", token);

				// Upgrade error if termination is in witness
				if (strchr(token, ';'))
					return FATAL;
				nWarn++;
				continue;
			} else if (ms == SUPPRESSED)
				continue;
			if (ms == BADHAND || hand > 0) {
				fWarn(ctx, "=", "No macros with correctors:", token);
				continue;
			}
			if (sub)
				macro->inset[ms] = NO;
			else
				macro->inset[ms] = YES;
			break;
		case '$':
			mac2 = getMacro(ctx, token);
			if (!mac2) {
				fWarn(ctx, "<", "Unknown macro:", token);
				nWarn++;
				continue;
			}
			for (ms = 0; ms < ctx->nMSS; ms++) {
				if (!mac2->inset[ms])
					continue;
				if (sub)
					macro->inset[ms] = NO;
				else
					macro->inset[ms] = YES;
			}
			break;
		case ';':
			return (nWarn == 0) ? OK : WARN;
		}
	}
	EOFWARN(ctx, "=");
	assert( token );
	return FATAL;
}

// Syntax: @ {verse}
static Status
	doVerse(Context *ctx)
{
	char *token;

	token = getToken(ctx);
	if (!token) {
		EOFWARN(ctx, "@");
		return FATAL;
	}
	strcpy(ctx->par[ctx->parallel].position, token);
	return OK;
}

// Syntax:	[ {lemma}* { | {*{n}} {var-state}+ }+ ]
static Status
	doReadings(Context *ctx)
{
	char *token;
	char *lem, *end;
	int lemma = YES, space = NO, var = -1;

	lem = ctx->lemma;
	end = &ctx->lemma[dimof(ctx->lemma)];
	*lem = EOS;

	ctx->piece++;
	ctx->pieceUnits[ctx->piece] = 0;
	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			if (lemma) {
				if (space)
					lem = append(lem, end, " ");
				lem = append(lem, end, token);
			} else {
				assert( var != -1 );
				assert( space == YES || ctx->nRdgs[var] == 0 );
				ctx->nRdgs[var]++;
			}
			space = YES;
			break;
		case '|':
			var = ctx->var++;
			assert( ctx->wgts[var] == 1 );	// Should've been init'd in initContext()
			if (token[1] == EOS)
				;
			else if (token[1] == '*')
				ctx->wgts[var] = strtol(token+2, (char **) 0, 0);
			else {
				char *end;
				long wgt = strtol(token+1, &end, 0);
				if (*end != EOS)
					wgt = 0; /* Handle conditional inclusion of scribal errors later */
				else if (wgt == 0)
					;        /* Explicit zero weights stay zero (should not be used) */
				else if (ctx->weighByED == 0)
					wgt = 1; /* Don't weight by edit distance */
				else
					wgt = (wgt-1)/ctx->weighByED + 1;
				ctx->wgts[var] = wgt;
			}
			ctx->wvar += ctx->wgts[var];
			ctx->pieceUnits[ctx->piece]++;
			lemma = NO;
			space = NO;
			break;
		case ']':
			return OK;
		}
	}
	EOFWARN(ctx, "[");
	return FATAL;
}

// Syntax:	< {states} {mss-names}+ { | {states} {mss-names}+ }+ >
static Status
	doWitnesses(Context *ctx)
{
	char *token, *rdgs = (char *) 0, *s;
	int states = YES, set, ms, hand;
	Macro *macro;
	int nWarn = 0;
	Parallel *para = &ctx->par[ctx->parallel];

	assert( ctx->mss );
	for (ms = 0; ms < ctx->nMSS; ms++) {
		register Testim  *t = &para->testim[ms];
		t->hands[0].level = 0;
	}

	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			if (states) {
				states = NO;
				if (strlen(token) != ctx->pieceUnits[ctx->piece]) {
					char buf[MAXTOKEN];
					sprintf(buf, "%s (%zd) should have exactly %d\n",
						token, strlen(token), ctx->pieceUnits[ctx->piece]);
					fWarn(ctx, "<", "Variant mismatch:", buf);
					return FATAL;
				}

				// Save off states for this set
				set = ctx->set++;
				assert( ctx->states[set] == (char *) 0 );

				rdgs = strdup(token);
				assert( rdgs );
				for (s = rdgs; (s = strchr(s, LACUNOSE)); )
					*s = MISSING;
				for (s = rdgs; (s = strchr(s, UNASSIGN)); )
					*s = MISSING;
				ctx->states[set] = rdgs;
			} else {
				//register Witness *w;
				register Testim  *t;
				ms = findMSS(ctx, token, &hand);
				if (ms == NOMSS || ms == BADHAND) {
					fWarn(ctx, "<", "Unknown:", token);
					if (*token == '<')
						return FATAL;
					nWarn++;
					continue;
				} else if (ms == SUPPRESSED)
					continue;
				//w = &ctx->mss[ms];
				t = &para->testim[ms];
				if (t->hands[hand].sets[ctx->piece]
				&& t->hands[0].level == MAXMACRO) {
					fWarn(ctx, "<", "Duplicate:", token);
					nWarn++;
					continue;
				}
				assert( rdgs );
				t->hands[hand].sets[ctx->piece] = rdgs;
				t->hands[hand].level = MAXMACRO;
			}
			break;
		case '$':
			macro = getMacro(ctx, token);
			if (!macro) {
				fWarn(ctx, "<", "Unknown macro:", token);
				nWarn++;
				continue;
			}
			for (ms = 0; ms < ctx->nMSS; ms++) {
				register Testim  *t = &para->testim[ms];
				if (!macro->inset[ms])
					continue;
				if (t->hands[0].level > macro->level)
					continue;
				else if (t->hands[0].level == macro->level) {
					fWarn(ctx, "<", "Duplicate macro:", token);
					nWarn++;
					continue;
				}
				t->hands[0].sets[ctx->piece] = rdgs;
				t->hands[0].level = macro->level;
			}
			break;
		case '|':
			states = YES;
			break;
		case '>':
			for (ms = (ctx->Root && ctx->parallel == 0) ? 1 : 0;
					ms < ctx->nMSS; ms++) {
				register Witness *w = &ctx->mss[ms];
				register Testim  *t = &para->testim[ms];
				if (t->hands[0].suppressed)
					continue;

				// Let implicit $? override macros
				if (para->pMacros['?']->inset[ms]
				&& t->hands[0].level <= para->pMacros['?']->level) {
					t->hands[0].sets[ctx->piece] = 0;
					continue;
				}

				// Warn if unassigned taxa are not in $?
				if (!t->hands[0].sets[ctx->piece]
				&& !para->pMacros['?']->inset[ms]) {
					fWarn(ctx, "<", "Unassigned:",
						parName(ctx, ctx->parallel, t, 0, w->name));
					nWarn++;
					continue;
				}
			}
			return (nWarn == 0) ? OK : WARN;
		case '"':
			EAT(ctx, token, '"');
			break;
		}
	}
	EOFWARN(ctx, "<");
	return FATAL;
}

// Syntax:	^ {file}
static Status
	doChron(Context *ctx)
{
	char *token, *fn;
	FILE *fpChron;
	char witness[80];
	int minD, midD, maxD;

	token = getToken(ctx);
	if (!token) {
		EOFWARN(ctx, "^");
		return FATAL;
	}

	fn = token;
	if (fn[0] == '~') {
		static char buf[MAXTOKEN];
		snprintf(buf, dimof(buf), "%s%s", getenv("HOME"), &fn[1]);
		fn = buf;
	}
	fpChron = fopen(fn, "r");
	if (!fpChron) {
		fWarn(ctx, "^", "Cannot open file:", token);
		return FATAL;
	}
	
	while (fscanf(fpChron, "%s %d %d %d", witness, &minD, &midD, &maxD) == 4) {
		int hand = 0;
		int ms = 0;

		while ((ms = findAland(ctx, witness, &hand, ms)) < ctx->nMSS) {
			int pp;
			for (pp = 0; pp < ctx->nParallels; pp++) {
				register Testim *t = &ctx->par[pp].testim[ms];
				int h;

				t->hands[hand].earliest = minD;
				t->hands[hand].average  = midD;
				t->hands[hand].latest   = maxD;

				if (hand != 0)
					continue;
				for (h = 1; h < MAXHAND; h++) {
					t->hands[h].earliest = minD;
					t->hands[h].average  = midD;
					t->hands[h].latest   = INT_MAX;
				}
			}
			ms++;
		}
	}
	
	fclose(fpChron);
	return OK;
}

// Syntax:	- {input-name}+ ;
static Status
	doSuppress(Context *ctx)
{
	char *token;
	int ms, hand;
	Macro *macro;
	Parallel *para = &ctx->par[ctx->parallel];
	Status status = OK;

	ms = 0;
	while ((token = getToken(ctx))) {
		switch (*token) {
			register Testim *t;
		default:
			ms = findMSS(ctx, token, &hand);
			if (ms == SUPPRESSED) {
				fWarn(ctx, "-", "Already suppressed:", token);
				status = WARN;
				continue;
			}
			if (ms == NOMSS || ms == BADHAND) {
				fWarn(ctx, "-", "Unknown:", token);
				status = WARN;
				continue;
			}
			t = &para->testim[ms];
			if (hand != 0)
				t->hands[hand].suppressed = YES;
			else {
				for (hand = 0; hand < MAXHAND; hand++)
					t->hands[hand].suppressed = YES;
			}
			break;
		case '$':
			macro = getMacro(ctx, token);
			if (!macro) {
				fWarn(ctx, "-", "Unknown macro:", token);
				status = WARN;
				continue;
			}
			for (ms = (ctx->Root) ? 1 : 0; ms < ctx->nMSS; ms++) {
				register Testim  *t = &para->testim[ms];
				if (!macro->inset[ms])
					continue;
				for (hand = 0; hand < MAXHAND; hand++)
					t->hands[hand].suppressed = YES;
			}
			break;
		case ';':
			return status;
		}
	}
	EOFWARN(ctx, "-");
	assert( token );
	return FATAL;
}

// Syntax:	" {comments}* "
static Status
	doComment(Context *ctx)
{
	char *token;

	while ((token = getToken(ctx)) != 0) {
		if (*token == '"')
			return OK;
	}
	EOFWARN(ctx, "(comment)");
	return FATAL;
}

// Syntax:	~ {input-name} {Aland-name} {print-name}
static Status
	doAlias(Context *ctx)
{
	char *token;
	int ms, hand;
	register Witness *w;

	token = getToken(ctx);
	if (!token) {
		EOFWARN(ctx, "~");
		return FATAL;
	}

	ms = findMSS(ctx, token, &hand);
	if (ms == SUPPRESSED)
		return OK;
	if (ms == NOMSS) {
		fWarn(ctx, "~", "Unknown:\n", token);
		return FATAL;
	}
	if (hand > 0 || ms == BADHAND) {
		fWarn(ctx, "~", "Cannot have a corrector:\n", token);
		return FATAL;
	}
	w = &ctx->mss[ms];
	assert( w );

	token = getToken(ctx);
	if (!token) {
		EOFWARN(ctx, "~");
		return FATAL;
	}

	if (w->Aland != w->name)
		free(w->Aland);
	w->Aland = strdup(token);

	token = getToken(ctx);
	if (!token) {
		EOFWARN(ctx, "~");
		return FATAL;
	}

	if (w->pname != w->name)
		free(w->pname);
	w->pname = strdup(token);

	return OK;
}

/* ------------------------------------------------------ 
||
||  Write Variant Readings file
||
*/

// Syntax: @ {verse}
static Status
	vrVerse(Context *ctx)
{
	char *token;

	token = getToken(ctx);
	fprintf(ctx->fpVr, "\n@ %s\n", token);
	return OK;
}

// Syntax:	[ {lemma}* { | {*{n}} {var-state}+ }+ ]
static Status
	vrReadings(Context *ctx)
{
	char *token;
	int lemma = YES, space = NO, var = 0, wvar = 0, rdg = 0;

	while ((token = getToken(ctx))) {
		switch (*token) {
		default:
			if (lemma && !space)
				fprintf(ctx->fpVr, "\n>     ");
			if (space)
				fprintf(ctx->fpVr, " ");
			if (!lemma)
				fprintf(ctx->fpVr, "%d=", ++rdg);
			space = YES;
			fprintf(ctx->fpVr, "%s", token);
			break;
		case '|':
			var = ctx->var++;
			wvar = (ctx->wvar += ctx->wgts[var]);
			rdg = 0;
			if (ctx->wgts[var] > 0)
				fprintf(ctx->fpVr, "\n%4d  ", wvar-1);
			else
				fprintf(ctx->fpVr, "\n----  ");
			lemma = NO;
			space = NO;
			break;
		case ']':
			fprintf(ctx->fpVr, "\n");
			return OK;
		case '"':
			{
				Status status = doComment(ctx);
				if (status != OK)
					return status;
			}
			break;
		}
	}
	return FATAL;
}

static void
	writeVr(Context *ctx)
{
	char *token;

	rewind(ctx->fpMss);
	ctx->var = 0;
	ctx->wvar = 0;
	while ((token = getToken(ctx)) && *token != '!') {
		switch (*token) {
		default:
			break;
		case '@':
			vrVerse(ctx);
			break;
		case '*':
		case '=':
		case '-':
		case '+':
			EAT(ctx, token, ';');
			break;
		case '<':
			EAT(ctx, token, '>');
			break;
		case '"':
			EAT(ctx, token, '"');
			break;
		case '[':
			vrReadings(ctx);
			break;
		}
	}
}

static int
	litStratum(int year)
{
	static int strattab[] = {
		100, 350, 450, 600, 775, 950, 1100, 1200, 1300, 1400, 1500, 1600, 9999,
	};
	int st;

	if (YearGran != -1)
		return (year + YearGran/2) / YearGran;

	for (st = 0; st < dimof(strattab); st++) {
		if (year <= strattab[st])
			return st;
	}
	return dimof(strattab);
}
