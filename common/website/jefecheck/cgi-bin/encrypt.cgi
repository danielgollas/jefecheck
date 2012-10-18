#!/usr/bin/perl -w
use strict;
use lib "/home/jefeco5/bin/licensingModules";

use cryptModule;
use CGI qw/:standard/;
print "Content-type: text/html\n\n";

my %cypherCombo = ( passphrase=>param('passphrase'), message=>param('message'));

print "pass: ".$cypherCombo{'passphrase'};
print "<br/>msg: ".$cypherCombo{'message'};

print "PORQUELLEGAAQUI<br/>";
my $cypher = encryptBlowfish(%cypherCombo);


print "ANOMA<br/>";
print "$cypher<br/>";

