# server — SANSparallel search server

Listens on a TCP port and answers protein sequence search requests
against a prebuilt suffix array database.

## Contents

| File | Description |
|------|-------------|
| `server` | Search server executable |
| `Makefile` | Compiles `server` from source |
| server.f, client_c.c, sais.[ch] | Fortran core + C helper functions |

## Usage

    nohup server <database> <port> <title> &

| Argument | Description | Example |
|----------|-------------|---------|
| `database` | Path to database index (without extension) | `/data/uniprot/uniprot` |
| `port` | TCP port to listen on | `54321` |
| `title` | Human-readable label for this database instance | `uniprot.Jan2026` |

Example:

    nohup server /data/uniprot/uniprot 54321 uniprot.Jan2026 &

The server reads `<database>.SAP` and `<database>.SRES` at startup.
Output is appended to `nohup.out` unless redirected:

    nohup server /data/uniprot/uniprot 54321 uniprot.Jan2026 > logs/server.log 2>&1 &

Record the PID if you need to stop the server later:

    echo $! > server.pid
    kill $(cat server.pid)

## Building

    make

Requires `openmpi, gfortran` and `gcc`.

## Database files

The database index is created by the `SA/` pipeline. The server expects
the following files to exist before startup:

| File | Created by |
|------|------------|
| `<database>.psq` | `saisformatdb.pl` |
| `<database>.SAP` | `sap8` |
| `<database>.SRES` | `sap8` |

See `SA/README.md` for database creation instructions.
