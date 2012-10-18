#!/usr/bin/perl -wT
use strict;

print "Content-type: text/html\n\n";

#use lib "licensingModules";
use lib "/home/jefeco5/bin/licensingModules";

use Purchase;
use License;
use enums;
use cryptModule;
use dbModule;
use smtpModule;
use licenseModule;
use untaintModule;

use CGI qw/:standard/;

open F, ">>log.txt" or die $!;



my %cypherCombo = ( passphrase=>param('passphrase'), message=>param('message'));

if (  keys(%cypherCombo) != 2 ) {
   print F "Error: could not read passphrase/message pair\n";
   close F;
   die "Error: could not read passphrase/message pair\n";
}

print "Got cypher combo!";

my $plainText = cryptModule::decryptBlowfish(%cypherCombo);
#my $plainText = "bla=ww\nrequestername=chpts\nclientname=chpts\ncompanyname=JefeCorp\nos=Linux\npurchaseconfirmationnumber=1234-5678-9101\nemail=gollas\@jefecorp.com\njefecheckversion=1.3.2\nmac=00:13:72:76:8d:53\n";

if ( !$plainText ) {
   print F "Error: could not decypher message\n";
   close F;
   die "Error: could not decypher message\n";
}

print "Message deciphered!";

my %parameters;

my @pairs = split(/\n/, $plainText);

my $param;
my $value;

foreach my $pair (@pairs) {
   ($param, $value) = split(/=/, $pair);
   $parameters{$param} = $value; 
}

print "<br/>ADDING REQUESTER IP BUBIBI<br/>";

$parameters{'requesterip'} = remote_host();

while ((my $k, my $v) = each(%parameters) ) {
   print "<br/>$k = $v";
} 

print "<br/>CHECKING NUMBER OF PARAMS $enums::PARAMS_SIZE<br/>";

#if ( keys(%parameters) != $enums::PARAMS_SIZE ) {
#   print F "Error: Incorrect number of arguments passed to the license generator\n";
#   close F;
#   die "Error: Incorrect number of arguments passed to the license generator\n";
#}
#else{
#   print "enums::PARAM_SIZE";
#}

print "<br/>UNTAINTING EMAIL<br/>";

print "$parameters{'email'}";

my $email = untaintModule::untaint($untaintModule::EMAIL, $parameters{'email'});
$parameters{'email'} = $email;

print "<br/>UNTAINTING PCN: $parameters{'purchaseconfirmationnumber'}<br/>";

my $pcn = untaintModule::untaint($untaintModule::PCN, $parameters{'purchaseconfirmationnumber'});
$parameters{'purchaseconfirmationnumber'} = $pcn;

#Check for existing user with email and pcn
print "<br/>CHECKING IF USER EXISTS<br/>";
if ( dbModule::checkForExistingUser($email, $pcn) > 0) {
   print "<\nUSER EXISTS\n>";

   print "\nChecking if license exists\n";
   my $mac = untaintModule::untaint($untaintModule::MAC, $parameters{'mac'});
   $parameters{'mac'} = $mac;

   my $jfcv =  untaintModule::untaint($untaintModule::VERSION, $parameters{'jefecheckversion'});
   $parameters{'jefecheckversion'} = $jfcv;

   my $licenseID = dbModule::checkForExistingLicense($pcn, $mac, $parameters{'os'}, $jfcv);
   print "POST\n";

   my $purchase = Purchase->new;
   my $license = License->new;
   my $left;

#Existing license
   if ( $licenseID > -1 ) {
      print "\nLICENSE EXISTS\n";
      $license = dbModule::getLicense($licenseID, $pcn);
      $purchase = dbModule::getPurchase($license->purchaseID);
      $left = dbModule::getNumberOfLicensesLeft($pcn);
      print "Sending existing license\n";

      #License does not exist, check for available
      if ( $left > 0 ) {
	 print "<br/>LICENSES LEFT, SENDING<br/>";
	 smtpModule::sendLicense($left, $license, $purchase, 1);
      }
      #NO more licenses left
      else {
	 print "No more licenses left, sending buyMore<br/>";
	 smtpModule::sendBuyMoreLicenses($license, $purchase);
      }
   }

#New License
   else {
      print "Creating new license\n";
      my $license = License->new;
      $license = licenseModule::generateLicense(%parameters);

      print "Inserting license into db\n";
      print "With pcn: $pcn<br/>";

      $licenseID = dbModule::insertLicense($pcn, $license);

      print "Done inserting with id=$licenseID\n";
      if ( $licenseID > 0 ) {
	 $license = dbModule::getLicense($licenseID, $pcn);
	 my $purchaseID = $license->purchaseID;

	 print "\n\nGetting purchase with pid=$purchaseID\n";
	 $purchase = dbModule::getPurchase($purchaseID);

	 print "\n\nGetting number of licenses left\n";
	 my $left = dbModule::getNumberOfLicensesLeft($pcn);
	 print "\nYou have left $left licenses\n";

	 print "Sending license to customer\n";
	 smtpModule::sendLicense($left, $license, $purchase, 0);
	 print "License sent\n";
      } else{
	 if ( $licenseID == -1 ) {
	    print "<br/>No such purchase, license not inserted<br/>";
	 }
	 if ( $licenseID == -2 ) {
	    print "<br/>Sending buy upgrade<br/>";
	    smtpModule::sendBuyUpgrade($license, $purchase, $jfcv);

	 }
      }
      close F;
   }
}
else {
   print "Error: User not registered\n";
   #die "Error: User not registered\n";
   close F;
}