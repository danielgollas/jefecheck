#!/usr/bin/perl -wT

use strict;
use LWP::UserAgent;

use lib "/home/jefeco5/bin/licensingModules";
#use lib "licensingModules";

use dbModule;
use smtpModule;
use untaintModule;
use PayPalTransaction;
use enums;

my $query;
# read post from PayPal system and add 'cmd'
read (STDIN, $query, $ENV{'CONTENT_LENGTH'});
$query .= '&cmd=_notify-validate';

my $ua = new LWP::UserAgent;
#my $req = new HTTP::Request 'POST','https://www.sandbox.paypal.com/cgi-bin/webscr';
my $req = new HTTP::Request 'POST','https://www.paypal.com/cgi-bin/webscr';

$req->content_type('application/x-www-form-urlencoded');
$req->content($query);
my $result = $ua->request($req);

my @pairs = split(/&/, $query);

if ( @pairs <= 0 ) {
   die "Received an invalid response from paypal";
}
my $pair;
my %vars;
my $count = 0;

foreach $pair (@pairs) {
   (my $name, my $value) = split(/=/, $pair);
   $value =~ tr/+/ /;
   $value =~ s/%([a-fA-F0-9][a-fA-F0-9])/pack("C", hex($1))/eg;
   $vars{$name} = $value;
   $count++;
}

my $receiverEmail = "contact\@jefecorp.com";
#my $receiverEmail = "contac_1235499630_per\@jefecorp.com";
$receiverEmail = untaintModule::untaint($untaintModule::EMAIL, $receiverEmail);

open F,">pplog.txt" or die "$!";
print "Content-type: text/html\n\n";

print F "Posting to paypal\n";
print F "Checking for errors\n";
print F "Result: ".$result->content."\n";
if ( $result->is_error ) {
   #Send error email

   print F "HTTP error\n";
   die "*HTTP error\n";
}
elsif ( lc($result->content) eq 'verified' ) {
   print F "Payment Verified\n";

   my $txnID = untaintModule::untaint($untaintModule::TXNID, $vars{'txn_id'});

   my $paymentStatus = $vars{'payment_status'};

   my $quantity = untaintModule::untaint($untaintModule::QUANTITY, $vars{'option_selection1'});

   my $paymentAmount = untaintModule::untaint($untaintModule::AMOUNT, $vars{'mc_gross'});

   my $paymentDate = $vars{'payment_date'};

   my $ppReceiverEmail = untaintModule::untaint($untaintModule::EMAIL, $vars{'receiver_email'});

   my $payerEmail = untaintModule::untaint($untaintModule::EMAIL, $vars{'payer_email'});

   my $jfcVersion = $vars{'option_selection2'};

   my $userEmail = untaintModule::untaint($untaintModule::EMAIL, $vars{'option_selection3'});

   print F "Checking for completed payment...\n";
   if ( lc($paymentStatus) eq 'completed' ) {
      while ((my $k, my $v) = each(%vars) ) {
	 print F "$k = $v<br/>";
      }
      print F "PAYMENT COMPLETE\n";

      #transaction does not exist, proceed
      print F "Checking TXNID: $txnID\n";
      my $pptxnid = dbModule::checkForExistingTXNID($txnID);
      if ( $pptxnid == -1 ) {
	 print F "TXNID: $txnID does not exist\n";
	 #Check that the pp receiver email is the same as ours
	 if ( $receiverEmail eq $ppReceiverEmail ) {
	    #check for valid amount/currency 
	    my $currency = $vars{'mc_currency'};
	    my $purchaseStatus = dbModule::checkForValidAmount($currency, $quantity, $paymentAmount, $jfcVersion);

	    my $purchase = Purchase->new;
	    $purchase->status($purchaseStatus);
	    $purchase->quantity($quantity);
	    $purchase->email($userEmail);
	    $purchase->version($jfcVersion);

	    print F "Inserting purchase...\n";
	    my  $pid = dbModule::insertPurchase($purchase);
	    if ( $pid >= 0 ) {
	       $purchase = dbModule::getPurchase($pid);
	       print F "Done, inserting PayPal Transaction\n";
	       my $ppTransaction = PayPalTransaction->new;
	       $ppTransaction->txnID($txnID);
	       $ppTransaction->amount($paymentAmount);
	       $ppTransaction->quantity($quantity);
	       $ppTransaction->purchaseID($pid);
	       $ppTransaction->date($paymentDate);
	       $ppTransaction->payerEmail($payerEmail);
	       $ppTransaction->userEmail($userEmail);
	       $ppTransaction->currency($currency);

	       my $pptid = dbModule::insertPayPalTransaction($ppTransaction);
	       print F "Done\n";
	       
	    print F "Purchase status: ".$purchase->status."\n";
	       if ( $pptid >= 0 ) {
		  if ( $purchase->status == $enums::PURCHASE_UNDERPAID || 
		     $purchase->status == $enums::PURCHASE_OVERPAID || 
		     $purchase->status == $enums::PURCHASE_INVALIDCURRENCY ) {
		     print F "Purchase amount not valid\n";
		     smtpModule::sendNotifyInvalidAmount($purchase, $ppTransaction)
		  }
		  elsif ( $purchase->status == $enums::PURCHASE_NOSUCHPRODUCT ) {
		     print F "NO SUCH PRODUCT!\n";
		     smtpModule::sendNotifyNoSuchProduct($purchase, $ppTransaction);
		  }
		  elsif ( $purchase->status == $enums::PURCHASE_OK ) {
		     print F "Purchase Validated!\n";
		     print F "Sending confirm\n";
		     smtpModule::sendPurchaseConfirmation($purchase, $ppTransaction);
		  }
	       }
	       else {
		  print F "Error: Could not insert PayPalTransaction\n";
		  close F;
		  die "Error: Could not insert PayPalTransaction\n";
	       }
	    }
	    else {
	       print F "Error: Could not insert purchase\n";
	       close F;
	       die "Error: Could not insert purchase\n";
	    }
	 }
	 else {
	    print F "Receiver email from PayPal: $ppReceiverEmail does not match $receiverEmail\n";
	    close F;
	    die;
	 }
      }
      #txn exists, die
      else {
	 print F "Transaction $txnID exists in DB with ID= $pptxnid!\n";
	 close F;
	 die;
      }
   } 
   # Payment is not complete
   else {
      print F "Payment is not complete\n";
      close F;
   }
}
print "content-type: text/plain\n\n";
close F;
