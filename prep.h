
#define MAXTOKEN 256
#define MAXMACRO 256

#define MAXHAND 4
#define MAXPARS 3

#define dimof(a) (sizeof a/sizeof a[0])
#define NO 0
#define YES 1
#define EOS '\0'

typedef struct hand Hand;
struct hand {
	char **sets;					// States for each piece set
	int earliest, average, latest;	// Chrono possibilities
	int stratum;					// Chronological stratum
	int suppressed;					// Suppressed?
	int mandated;					// Mandated for output file?
	int inLacuna;					// Witness is lacunose (defaults to MISSING)
	int level;						// Priority level for current piece
	int lastHand;					// Previous hand
};

typedef struct witness Witness;
struct witness {
	char *name;					// Name of the witness
	char *Aland;				// Gregory-Aland name
	char *pname;				// Print name (for translations)
	int corrected;				// Has a corrector.
};

typedef struct macro Macro;
struct macro {
	int level;					// Priority level of macro * = 0, ? = hi
	int *inset;					// Set of witnesses in the set
};

typedef struct parallel Parallel;
struct parallel {
	int name_space;				// Character code for Parallel.
	char position[MAXTOKEN];	// Current position in the .mss file
	Hand **msHands;				// MSS x Hand, 0=original, 1=first corrector
	Macro **pMacros;			// 256 macros, indexed by character
};

typedef struct context Context;
struct context {
	unsigned long lineno;		// Current Line Number
	int inc_line_p;				// Increment line number?
	char token[MAXTOKEN];		// Current Token
	char lemma[25];				// Current lemma
	unsigned long token_lineno;	// Line Number of Token

	FILE *fpMss;				// .mss file (in)
	FILE *fpTx;					// .tx file (out)
	FILE *fpVr;					// .vr file (out)
	FILE *fpNo;					// .no file (out)

	int nParallels;				// Number of Parallel Witnesses
	int parallel;				// Which parallel
	Parallel par[MAXPARS];		// Set of Parallel Witnesses
	
	int nMSS;					// Number of MSS
	Witness *mss;				// Each of the witnesses

	char **subset;              // Selected subset of witnesses (from command-line)

	int nVar;					// Number of variation units
	int var;					// Current variation unit
	int wvar;					// Current weighted variation unit
	int *nRdgs;					// Number of readings for each unit
	int *wgts;					// Weight of each unit, *0 means suppress
	int weighByED;				// Weigh variants by provided edit-distance

	int nPiece;					// Number of pieces (complex var units)
	int piece;					// Current piece
	int *pieceUnits;			// Number of variation units in each piece

	int nSets;					// Number of sets
	int set;					// Current set
	char **states;				// States of each set

	int macLevel;				// Macro level

	char *Root;					// Has root? If so, its name.
};

#define NO  0
#define YES 1
#define EOS '\0'

#define new(n,type) malloc((n) * sizeof (type))
