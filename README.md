# prep - Prepare Collations  for Stemma
**Prep** is a command-line utility written in C that preprocesses textual collation data for use with the **stemma** program. It processes a collation file containing manuscript variant information and generates three output files: a matrix of taxa and variants (`.tx`), stratigraphical constraints (`.no`, and a listing of each variant (`.vr`). These outputs are tailored for stemmatic analysis, enabling the **stemma** program to infer relationships between manuscripts.
## Overview
In textual criticism, a collation compares different manuscripts or versions of a text to identify variations. **Prep** takes such collation data, optionally filters it based on user-defined criteria (e.g., fragmentary witnesses, corrected manuscripts, or chronological constraints), and transforms it into structured formats suitable for phylogenetic or stemmatic analysis.
Usage
```
bash
prep collation_file [taxa...]
```
* **collation_file**: The input file containing collation data (typically with no extension).
** `taxa...`: An optional space-separated list of specific manuscript names (taxa) or macros defined in the file to include in the output. If omitted, all witnesses are processed.
## Example
Preprocess a collation file named `mss`:
```
bash
./prep mss
```
Preprocess only specific manuscripts:
```
bash
./prep mss MS1 MS2 MS3
```
## Input Format
The collation file uses a specific syntax with tokens to define manuscripts, variants, and metadata. Key tokens include:
* `*`: Defines the semicolon terminated list of manuscripts (witnesses), e.g., `* MS1 MS2 MS3 ;`.
* `/`: Switches between parallel texts, e.g., `/a` for parallel 'a'. Useful for Synoptic gospels.
* `=`: Defines or modifies macros (for sets of manuscripts), e.g., `= $m MS1 MS2 ;` sets macro `$m`.
* `%`: Manages lacunae (gaps), e.g., `%- MS1 ;` marks MS1 as lacunose starting from that point in the text.
* `@`: Marks a verse or section, e.g., `@ John 1:1`.
* `[`: Begins a list of readings (variants), e.g., `[ lemma | reading1 | reading2 ]`.
* `<`: Begins a list of witnesses for readings, e.g., `< 00 MS1 MS2 | 11 MS3 >` (NB: the number of digits in the first item must equal the number of readings in the previous list of readings).
* `^`: Specifies a chronological file, e.g., `^ chron.txt`.
* `~`: Defines aliases, e.g., `~ MS1 GA123 PrintName` (useful if your collation with one notation and your chron information in another).
* `"`: Encloses double-quotation terminated comments, e.g., `" This is a comment "`.
* `-`: Removes collated manuscripts from the output, e.g., `- MS1 ;`.
* `+`: Skips tokens until the semicolon terminator, e.g., `+ junk ;` (useful for commenting out macro definitions, etc.).
For detailed syntax, refer to the comments in prep.c under the Tokens: section.
## Sample Input
```
* MS1 MS2 MS3 ;
@ John 1:1
[ In principio | erat | fuit ]
< 0 MS1 MS2 | 1 MS3 >
```
## Output Files
**Prep** generates three files from the input collation:
1. `mss.tx`:
  * A matrix where rows represent taxa (manuscripts) and columns represent weighted variants.
  * Each cell indicates the variant state (e.g., `'0'`, `'1'`, or `'?'`) for a manuscript.
  * Format: `<number of witnesses> <number of weighted variants>` followed by rows of witness names and variant states.
1. `mss.no`:
  * Stratigraphical constraints based on chronological data (if provided via a `^` token).
  * Lists each witness with its stratum (time layer) and manuscripts it postdates.
  * Format: `<witness> <stratum> < <preceding witnesses> >`.
1. `mss.vr`:
  A listing of each variant unit, including the lemma and possible readings.
  Format: Verse markers (`@`) followed by lemma and numbered readings (e.g., `> In principio\n1=erat 2=fuit`).
  These files are designed as inputs for the Stemma program for further analysis.
## Environmental Variables
Customize Prep's behavior with environment variables (set via export in bash or prefixed on the command line calling prep):
* `YEARGRAN`:
  Controls year granularity for stratigraphy.
  * Default: 0 (no granularity).
  * -1: Tuned for New Testament manuscripts (uses a predefined table).
  * Positive integer: Groups years into specified intervals.
* `FRAG`:
  * Threshold for including fragmentary witnesses (as a percentage or number of non-constant variants).
  * Default: `50%`.
  * Example: `export FRAG=75%` or `export FRAG=10`.
* `CORR`:
  * Threshold for including corrected witnesses (as a percentage or number of new variants).
  * Default: `10`%.
  * Example: `export CORR=20%`.
* `YEAR`:
  * Cutoff year; excludes witnesses dated after this year.
  * Default: No cutoff.
  * Example: `export YEAR=1200`.
* `NOSING`:
  * If set (e.g., export `NOSING=1`), excludes singular readings (variants unique to one witness) from the matrix.
* `ROOT`:
  * Defines an explicit root or ancestor (e.g., the archetype or a standard text).
  * Default: No root.
  * Example: `export ROOT=UBS`.
* `KEEPSAME`:
  * Turns off the identical witness check.
  * Default: suppress identical witnessesd
  * Example: `export KEEPSAME`
## Special Macros
*  `$*`: Represents all active witnesses.
*  `$?`: Represents witnesses with unknown readings (e.g., lacunae or missing data).
## License
This project is licensed under the MIT License. See the LICENSE file for details.
## Acknowledgements
* The prep program is designed to work with the **stemma** program for stemmatic analysis.
