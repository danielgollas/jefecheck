#USAGE run with 32 or 64 as arguments to create an installation package for linux32 or 64
#second argument should be _demo or blank

DIR=JefeCheck_Linux_x86-$1$2_Install
exeFile=jefecheck$2

#cleanup
echo removing $DIR
rm -Rf $DIR
echo removing ${DIR}.tar.gz
rm   ${DIR}.tar.gz

#create the structure
mkdir  ./${DIR}
mkdir ./${DIR}/Received
mkdir ./${DIR}/lib
mkdir ./${DIR}/bin
mkdir ./${DIR}/FX

#copy the executable and append _Demo if  necesary
cp -v  ../optimized/src/checkmate1 ./${DIR}/bin/$exeFile
strip  ./${DIR}/bin/$exeFile
#copy the libs
cp -v ./libs$1/* ./${DIR}/lib/

#create the installation scripts
#installScript= InstallJefeCheck{$2}.sh

#copy the install scripts, we use the Demo versions if we are creating a demo package
#cp -v ./installJefeRunAsRoot$2.sh ./${DIR}
cp -v ./installJefeRunAsUser$2.sh ./${DIR}

#copy the common resources
cp  ../CommonResources/FX/* ./${DIR}/FX/
cp ../CommonResources/Manual/JefeCheckManual.pdf ./${DIR}/
cp ../CommonResources/Manual/JefeCheckQuickStart.pdf ./${DIR}/

#copy readme
cp  ./README.linux ./${DIR}

#tar gzip 
echo creating tarball $DIR.tar.gz
tar czf ${DIR}.tar.gz $DIR
#gzip -9 ${DIR}.tar 

#cleanup
echo cleaning up
rm -Rf $DIR

#sudo ls

echo done!

#cp -fv  ${DIR}.tar.gz /home/dgollas/JEFECHECK/v1.0/LINUX/





