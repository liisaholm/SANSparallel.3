# client — SANSparallel search client

Sends protein sequences to a running SANSparallel server and returns
results as XML. Output can be piped directly to `scripts/XMLParser.py`
for tabular format.

## Contents

| File | Description |
|------|-------------|
| `client.c` | C source |
| `Makefile` | Compiles `client` from source |

## Building

    make

Requires `gcc`.

## Usage

    client -H <hostname> -P <port> [options] < input.fasta > output.xml

Input is FASTA on stdin. Output is SANSparallel XML on stdout.

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `-H hostname` | Server hostname | |
| `-P port` | Server port number | |
| `-m protocol` | Search mode: `-1` verifast, `0` fast, `1` slow | `0` |
| `-E evalue` | E-value threshold | |
| `-s minscore` | Minimum vote score | |
| `-T minkeyscore` | Minimum key score | |
| `-V length` | Vote list length | |
| `-h maxaccepts` | Maximum accepts | |
| `-R maxrejects` | Maximum rejects | |
| `-W width` | Vote cluster width | |
| `-w tubewidth` | DP tube width | |
| `-x width` | SANS window width | |

## Examples

Basic search against a local server:

    client -H localhost -P 54321 < query.fasta > results.xml

Pipe directly to tabular output:

    client -H localhost -P 54321 < query.fasta | python ../scripts/XMLParser.py > results.tsv

Sensitive search with e-value cutoff:

    client -H localhost -P 54321 -m 1 -E 1e-5 < query.fasta > results.xml

## See also

- `server/README.md` — starting the search server
- `scripts/README.md` — converting XML output to tabular format
- `SA/README.md` — building the sequence database
