# SANSparallel

SANSparallel performs fast protein database search using adaptive seeds.

## Components

| Directory | Description |
|-----------|-------------|
| `SA/` | Suffix array database construction |
| `server/` | MPI search server |
| `client/` | Search client — reads FASTA from stdin, returns XML |
| `scripts/` | Output processing (XML to tabular) |
| `tests/` | Integration test |

See the README.md in each subdirectory for details.

## Installation

Clone the repository and build each component:

    git clone git@github.com:liisaholm/SANSparallel.git
    cd SANSparallel

    make -C server
    make -C client
    make -C SA

Each `make` compiles the binaries in place — no system-wide installation
is performed. See the README.md in each subdirectory for prerequisites.

## Licence

See `LICENCE`.

## Citation

If you use SANSparallel in your work, please cite:

Somervuo P, Holm L (2015) SANSparallel: interactive homology search
against Uniprot. *Nucleic Acids Res.* 43, W24-29.
https://doi.org/10.1093/nar/gkv317
