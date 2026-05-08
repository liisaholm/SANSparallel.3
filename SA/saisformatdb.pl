#!/usr/bin/perl
use strict;
use warnings;
use File::Basename;
use FindBin qw($Bin); # $Bin is the directory where the script is located

# --- Configuration & Environment ---
my $conf_file = $ENV{SANS_CONF} || "$Bin/sans.conf";

die "Config file not found: $conf_file\n" unless -r $conf_file;
my %cfg = read_config($conf_file);

# Set the library path globally from config or default
$ENV{LD_LIBRARY_PATH} = $cfg{LIB_PATH} || "/usr/local/lib";

# Executables now localized to the script's directory
my $ISA_EXE     = "$Bin/isa8"; 
my $SAP_EXE     = "$Bin/sap8";
my $SUFTEST_EXE = $cfg{SUFTEST_EXE}; 

# Check that they exist and are executable
foreach my $exe ($ISA_EXE, $SAP_EXE) {
    die "Executable not found or not runnable: $exe\n" unless -x $exe;
}

if (@ARGV < 2) {
    die "USAGE: $0 db-name fastafiles\n";
}

my ($project, @fastafiles) = @ARGV;
my $MAXRES      = 32_000;
my $REMOVE_GAPS = 0;

# --- Main Logic ---

# 1. Create .phr, .psq, and .pin files
warn "# Parsing FASTA files into project: $project\n";
splitter($project, $REMOVE_GAPS, @fastafiles);

# 2. Run Indexing Pipeline
my @pipeline = (
    ["$SUFTEST_EXE $project.psq $project.SA", "SufTest/SA generation"],
    ["$ISA_EXE $project",                     "ISA generation"],
    ["$SAP_EXE $project",                     "SAP/SRES generation"]
);

foreach my $step (@pipeline) {
    my ($cmd, $desc) = @$step;
    warn "# Running $desc...\n";
    system_cmd($cmd);
}

warn "# Finished ... you may delete intermediate results $project.SA and $project.ISA\n";

exit(0);

# --- Subroutines ---

sub splitter {
    my ($project, $remove_gaps, @files) = @_;
    
    # Open filehandles once since no_split is assumed
    open(my $fh_phr, '>', "$project.phr") or die "Cannot open $project.phr: $!";
    open(my $fh_psq, '>', "$project.psq") or die "Cannot open $project.psq: $!";
    open(my $fh_pin, '>', "$project.pin") or die "Cannot open $project.pin: $!";

    my ($ptr, $hdr_ptr) = (0, 0);
    my ($seq, $header)  = ('', '');

    # Determine if input is compressed
    my $is_gz = ($files[0] =~ /\.gz$/) ? 1 : 0;
    foreach my $f (@files) {
        if (($f =~ /\.gz$/ ? 1 : 0) != $is_gz) {
            die "Error: Can't mix compressed (.gz) and uncompressed inputs!\n";
        }
    }

    my $input_cmd = $is_gz ? "gzip -dc @files |" : "cat @files |";
    open(my $in, $input_cmd) or die "Failed to open input pipe: $!";

    while (<$in>) {
        chomp;
        if (/^>(.*)$/) {
            my $new_header = $1;
            if ($seq ne '') {
                ($ptr, $hdr_ptr) = write_entry(\$seq, \$header, \$ptr, \$hdr_ptr, $fh_phr, $fh_psq, $fh_pin);
            }
            $header = $new_header;
            $seq    = '';
        } else {
            s/\s//g if $remove_gaps;
            $seq .= $_;
        }
    }

    # Process final entry
    if ($seq ne '') {
        write_entry(\$seq, \$header, \$ptr, \$hdr_ptr, $fh_phr, $fh_psq, $fh_pin);
    }

    close($_) for ($fh_phr, $fh_psq, $fh_pin, $in);
}

sub write_entry {
    my ($s_ref, $h_ref, $p_ref, $hp_ref, $fh_phr, $fh_psq, $fh_pin) = @_;

    if (length($$s_ref) > $MAXRES) {
        warn "# Truncating sequence (" . length($$s_ref) . " aa > $MAXRES)\n";
        $$s_ref = substr($$s_ref, 0, $MAXRES);
    }

    my $lseq = length($$s_ref);
    my $lhdr = length($$h_ref);

    $$s_ref = uc($$s_ref);
    print $fh_phr "$$h_ref\n";
    print $fh_psq "$$s_ref\n";
    print $fh_pin join("\t", $$p_ref, $lseq, $$hp_ref, $lhdr), "\n";

    # Update pointers for return
    my $new_ptr     = $$p_ref  + $lseq + 1;
    my $new_hdr_ptr = $$hp_ref + $lhdr + 1;

    return ($new_ptr, $new_hdr_ptr);
}

sub system_cmd {
    my $cmd = shift;
    my $rc = system($cmd);
    die "Command failed with exit code $rc: $cmd\n" if $rc != 0;
}

sub read_config {
    my $file = shift;
    my %config;
    open(my $fh, '<', $file) or die "Can't open config $file: $!";
    while (<$fh>) {
        chomp;
        next if /^\s*#/ || /^\s*$/;
        if (/^\s*(\w+)\s*=\s*(.*)\s*$/) {
            $config{$1} = $2;
        }
    }
    return %config;
}
