# tests — SANSparallel integration test

## Contents

| File | Description |
|------|-------------|
| `run_test.sh` | Integration test script |
| `data/sample.fasta` | Small FASTA file used to build the test database |
| `data/query.fasta` | Query sequence(s) with a known expected hit |

## Usage

Run from the repository root:

    bash tests/run_test.sh


## What the test does

1. Builds a mini suffix array database from `data/sample.fasta`
2. Starts the server on a local port via `mpirun -np 2`
3. Runs the client with `data/query.fasta` as input
4. Parses the XML output to TSV via `XMLParser.py`
5. Checks that a known expected hit appears in the results
6. Cleans up the temporary database and stops the server

The server is stopped automatically on exit, whether the test passes or fails.

## Dependencies

The test requires all components to be compiled before running:

    cd /path/to/SANSparallel/server; make 
    cd /path/to/SANSparallel/client; make
    cd /path/to/SANSparallel/SA; make 
