copy ..\..\installers\*.* .\jefecheck\binaries\
@echo off
echo user jefeco5> ftpcmd.dat
set choice=
set /p choice=Enter password:
echo %choice%>> ftpcmd.dat
echo bin>> ftpcmd.dat
echo hash>> ftpcmd.dat
echo cd www >> ftpcmd.dat
echo cd jefecheck >> ftpcmd.dat
echo mkdir binaries >> ftpcmd.dat
echo cd binaries >> ftpcmd.dat
echo mput C:\projects\jefecheck2\common\website\jefecheck\binaries\Jefe*.*>> ftpcmd.dat
echo cd .. >> ftpcmd.dat
echo cd images >> ftpcmd.dat
REM echo mput C:\projects\jefecheck2\common\website\jefecheck\images\*.png >> ftpcmd.dat
echo cd .. >> ftpcmd.dat
echo mput C:\projects\jefecheck2\common\website\jefecheck\*.html>> ftpcmd.dat
echo quit>> ftpcmd.dat
ftp -n -i -s:ftpcmd.dat ftp.jefecorp.com
REM del ftpcmd.dat