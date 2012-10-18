<?php
include("global.inc.php");
$errors=0;
$error="The following errors occured while processing your form input.<ul>";
pt_register('POST','name');
pt_register('POST','email');
pt_register('POST','os');
pt_register('POST','company');
pt_register('POST','industry');
pt_register('POST','country');
pt_register('POST','phone');
pt_register('POST','hear');
pt_register('POST','updates');
if($name=="" || $email=="" || $os=="" || $company==""){
$errors=1;
$error.="<li>You did not enter one or more of the required fields. Please go back and try again.";
}
if(!eregi("^[a-z0-9]+([_\\.-][a-z0-9]+)*" ."@"."([a-z0-9]+([\.-][a-z0-9]+)*)+"."\\.[a-z]{2,}"."$",$email)){
$error.="<li>Invalid email address entered";
$errors=1;
}
if($errors==1) echo $error;
else{
$where_form_is="http".($HTTP_SERVER_VARS["HTTPS"]=="on"?"s":"")."://".$SERVER_NAME.strrev(strstr(strrev($PHP_SELF),"/"));
$message="name: ".$name."
email: ".$email."
os: ".$os."
company: ".$company."
industry: ".$industry."
country: ".$country."
phone: ".$phone."
hear: ".$hear."
updates: ".$updates."
";
$message = stripslashes($message);
mail("jefecheckbetadownloads@jefecorp.com","Form Submitted at your website",$message,"From: phpFormGenerator");
$link = mysql_connect("localhost","jefeco5_betaDown","truco10");
mysql_select_db("jefeco5_betaDownloaders",$link);
$query="insert into downloaders (name,email,os,company,industry,country,phone,hear,receiveupdates) values ('".$name."','".$email."','".$os."','".$company."','".$industry."','".$country."','".$phone."','".$hear."','".$updates."')";
mysql_query($query);

header("Refresh: 0;url=http://jefecheck.jefecorp.com/download.html");
?><?php 
}
?>