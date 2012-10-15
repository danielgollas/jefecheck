<?php ob_start() ?>
<?php
if ($_GET['randomId'] != "tNMUxNGbgHVw5xPwB92fWSXIAgyg8bzpq_XnKXsp8e19MA1_MmeeGFDp4IyjGPzRL62yYxFqdqBhfS9pi1gLCfYJNYVNzOwidmDdQzLBb_yEc_Wh9AeSUOk0JpPjkqeC_QsXwRO3JsESO999zVXME5BXNcFzzLmQBHwjoOevcheBfNfNKxoH90odbLmEsNOHb4b1JMECdw04NgfcchAtm6_qLnNwjjetTswx0tVqzinBaQ8Bzkzb8C5NCDnehjVw") {
    echo "Access Denied";
    exit();
}
?>
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
<title>Editing form1.html</title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<style type="text/css">body {background-color:threedface; border: 0px 0px; padding: 0px 0px; margin: 0px 0px}</style>
</head>
<body>
<div align="center">
<script language="javascript">
<!--//
// this function updates the code in the textarea and then closes this window
function do_save() {
	var code =  htmlCode.getCode();
	document.open();
	document.write('<html><form METHOD="POST" name=mform action="http://www.jefecheck.com:2082/frontend/x/files/savehtmlfile.html"><input type="hidden" name="dir" value="/home/jefeco5/public_html/jefecheck/DownloadBetaForm"><input type="hidden" name="file" value="form1.html">Saving&nbsp;....<br /><br ><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><br /><textarea name=page rows=1 cols=1></textarea></form></html>');
	document.close();
	document.mform.page.value = code;
	document.mform.submit();
}
function do_abort() {
	var code =  htmlCode.getCode();
	document.open();
	document.write('<html><form METHOD="POST" name="mform" action="http://www.jefecheck.com:2082/frontend/x/files/aborthtmlfile.html"><input type="hidden" name="dir" value="/home/jefeco5/public_html/jefecheck/DownloadBetaForm"><input type="hidden" name="file" value="form1.html">Aborting Edit&nbsp;....</form></html>');
	document.close();
	document.mform.submit();
}
//-->
</script>
<?php
// make sure these includes point correctly:
include_once ('/usr/local/cpanel/base/3rdparty/WysiwygPro/editor_files/config.php');
include_once ('/usr/local/cpanel/base/3rdparty/WysiwygPro/editor_files/editor_class.php');

// create a new instance of the wysiwygPro class:
$editor = new wysiwygPro();

// add a custom save button:
$editor->addbutton('Save', 'before:print', 'do_save();', WP_WEB_DIRECTORY.'images/save.gif', 22, 22, 'undo');

// add a custom cancel button:
$editor->addbutton('Cancel', 'before:print', 'do_abort();', WP_WEB_DIRECTORY.'images/cancel.gif', 22, 22, 'undo');

$body = '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
<title>My Form - created with phpFormGenerator</title>
<meta content="text/html; charset=utf-8" http-equiv="Content-Type">
</head>
<body>
<div align="center"><font size="2" face="arial" color="#666666"><strong>All fields marked with a * are required: </strong></font></div>
<form enctype="multipart/form-data" action="process.php" method="post">
<table bordercolor="#000000" border="1"><tbody><tr></tr><tr></tr></tbody></table>
<table width="68%" border="0" align="center"> <tbody><tr> <td width="51%" bgcolor="#c0c0c0"><font face="Arial, Helvetica, sans-serif"> Your Name<font color="#ff0000">*</font></font></td> <td width="49%" bgcolor="#c0c0c0">
<input type="text" size="80" name="name"></td></tr><tr> <td bgcolor="#cccccc"><font face="Arial, Helvetica, sans-serif">Your Email<font color="#ff0000">*</font></font></td> <td bgcolor="#cccccc">
<input type="email" name="email"></td></tr><tr> <td bgcolor="#c0c0c0"><font face="Arial, Helvetica, sans-serif">What Operating System will this demo be installed on?<font color="#ff0000">*</font></font></td> <td bgcolor="#c0c0c0"><select name="os">
<option value="Windows XP">Windows XP</option>
<option value="Windows Vista">Windows Vista</option>
<option value="Mac OS X">Mac OS X</option>
<option value="Linux">Linux</option></select></td></tr><tr> <td bgcolor="#cccccc"><font face="Arial, Helvetica, sans-serif"> Your Company\'s Name</font></td> <td bgcolor="#cccccc">
<input type="text" size="80" name="company"></td></tr><tr> <td bgcolor="#c0c0c0"><font face="Arial, Helvetica, sans-serif"> What does your company do?</font></td> <td bgcolor="#c0c0c0">
<input type="text" size="80" name="industry"></td></tr><tr> <td bgcolor="#cccccc"><font face="Arial, Helvetica, sans-serif"> What Country is your company from?</font></td> <td bgcolor="#cccccc">
<input type="text" size="80" name="country"></td></tr><tr> <td bgcolor="#c0c0c0">
<div align="left"><font face="Arial, Helvetica, sans-serif">Phone Number (don\'t fill if you don\'t whish us to contact you by phone)</font></div></td> <td bgcolor="#c0c0c0">
<input type="text" size="35" name="phone"></td></tr><tr> <td bgcolor="#cccccc"><font face="Arial, Helvetica, sans-serif"> Where did you here about JefeCheck?</font></td> <td bgcolor="#cccccc">
<input type="text" size="80" name="hear"></td></tr><tr> <td bgcolor="#c0c0c0"><font face="Arial, Helvetica, sans-serif"> Would you like to receive news about updates, release dates and other JefeCheck related information?</font></td> <td bgcolor="#c0c0c0">
<input type="checkbox" name="updates"></td></tr></tbody></table>
<div align="center"><font size="2" face="arial"><strong>
<input type="submit" value="Submit Form and Proceed to Download Area">
<input type="reset" value="Clear Form"> </strong>
<br>
<br>
<br> <a href="http://phpformgen.sourceforge.net">
<img border="0" alt="" src="button.jpg"></a></font></div></form>
</body>
</html>';

$editor->set_code($body);

// add a spacer:
$editor->addspacer('', 'after:cancel');

// print the editor to the browser:
$editor->print_editor('100%',450);

?>
</div>
</body>
</html>
<?php ob_end_flush() ?>
