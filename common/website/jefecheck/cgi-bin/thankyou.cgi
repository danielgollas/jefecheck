#!/usr/bin/perl 

#use strict;
print "Content-type: text/html\n\n"; 
print "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Transitional//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd'>\n";
print "<html xmlns='http://www.w3.org/1999/xhtml'>\n";
print "<head>\n";
print "<script type='text/javascript'>\n";
print "function redirectToThankyou()\n";
print "{\n";
print "window.location='../thankyou.html';\n";
print "}\n";
print "</script>";
print "</head>\n";

print "<body onload='redirectToThankyou();'>\n";

print "</body>\n";
print "</html>\n";
