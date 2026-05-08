# scripts — SANSparallel output processing

## XMLParser.py

Converts raw SANSparallel XML output to a tab-separated table.

### Input format

SANSparallel returns results as in-house XML with one `<QUERY>` block
per input sequence, each containing one or more `<SBJCT>` blocks plus
plain FASTA sequences for display in HTML pages.

```
<QUERY nid=... vote_cutoff=... LSEQ=...>
    <SBJCT VOTE=... TUPS=... PIDE=... LALI=... BITS=... EVALUE=... DIAG=... LSEQ=...>
    ...FASTA...
    </SBJCT>
</QUERY>
```

### Output columns

| Column | Description |
|--------|-------------|
| `nid` | Query identifier |
| `isquery`  | Flag for query row |
| `VOTE` | Diagonal vote score |
| `TUPS` | Tuple count |
| `PIDE` | Percent identity |
| `LALI` | Alignment length |
| `BITS` | Bit score |
| `EVALUE` | E-value |
| `DIAG` | Diagonal index |
| `LSEQ` | Subject sequence length |
| `qpid` | Query protein identifier |
| `spid` | Subject protein identifier |
| `qcov` | Query coverage (LALI/LSEQ) |
| `scov` | Subject coverage (LALI/LSEQ) |
| `desc` | Subject description |
| `species` | Subject species |
| `gene` | Subject gene name |
| `rank` | Hit rank |
| `qseq` | Query sequence (FASTA) |
| `sseq` | Subject sequence (FASTA) |

### Usage

    python XMLParser.py < input.xml > output.tsv

or as part of a pipeline:

    sans_client ... | python XMLParser.py > output.tsv
