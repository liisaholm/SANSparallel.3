# SA — Suffix Array database construction

Builds a SANSparallel search database from one or more FASTA files.

## Contents

| File | Description |
|------|-------------|
| `saisformatdb.pl` | Main script — drives the full pipeline |
| `sans.conf` | Site-specific paths (not in version control) |
| `sans.conf.example` | Template for `sans.conf` |
| `mksary64` | 64-bit suffix array construction (external binary, see below) |
| `isa8` | Builds inverse suffix array (8-byte addresses) |
| `isa8.f` | Fortran source for `isa8` |
| `sap8` | Builds SAP and sres tables |
| `sap8.f` | Fortran source for `sap8` |
| `Makefile` | Compiles `isa8` and `sap8` from source |

## Usage

    perl saisformatdb.pl <db-name> file1.fasta.gz [file2.fasta.gz ...]

Example — canonical UniProt build:

    perl saisformatdb.pl uniprot \
        /data/uniprot/uniprot_sprot.fasta.gz \
        /data/uniprot/uniprot_trembl.fasta.gz

The script reads `sans.conf` from the same directory and sets
`LD_LIBRARY_PATH` internally, so no environment setup is needed before
running.

## Pipeline

    saisformatdb.pl
        └── splitter()        — parse FASTA, write <db>.psq / .phr / .pin
        └── mksary64          — construct 64-bit suffix array → <db>.SA
        └── isa8              — build inverse suffix array   → <db>.ISA
        └── sap8              — build SAP and sres tables    → <db>.SAP / .SRES

## Configuration

Copy `sans.conf.example` to `sans.conf` and edit for your site:

    cp sans.conf.example sans.conf

| Key | Description | Default in example |
|-----|-------------|-------------------|
| `SUFTEST_EXE` | Path to the `mksary64` binary | `/data/uniprot/mksary64` |
| `LD_LIB_PATH` | Library path required by `mksary64` | `/usr/local/lib` |

`sans.conf` is excluded from version control (see `.gitignore`).

## Dependencies

**`isa8` and `sap8`** are compiled from the Fortran sources in this directory:

    make

**`mksary64`** is a pre-built binary not included in this repository.
It requires `libdivsufsort64` installed in `LD_LIB_PATH`.

To build `libdivsufsort64` from source:

    git clone https://github.com/y-taka-23/libdivsufsort
    cd libdivsufsort && mkdir build && cd build
    cmake -DBUILD_DIVSUFSORT64=ON .. && make && sudo make install
