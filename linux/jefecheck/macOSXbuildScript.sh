#sets up the environment variables for building jefecheck from the repository, configures and makes the executable. Builds the bundle also.
#specify if PPC or x86 as first parameter
#specify DEMO or blank as second parameter (the source must have the changes to build the demo, it won't do it by itself)

echo "BUILDING JEFECHECK" $2
 
export glutLibCommand="-framework glut -framework IOKit"
export boostLibPath="/usr/local/lib"
export boostThreadLibs="libboost_thread-xgcc40-mt-1_37.a"
export boostProgramOptionsLibs="libboost_program_options-xgcc40-mt-1_37.a"
export boostSystemLibs="libboost_system-xgcc40-mt-1_37.a"
export boostFileSystemLibs="libboost_filesystem-xgcc40-mt-1_37.a"

export gflLibs="-ldl -lgfl -lgfle"
export filLibs="-lfreeimage"

export botanLibs="-L/usr/local/lib -lm -lpthread /usr/local/lib/libbotan.a"
export ssllibPath="-lssl -lcrypto -lkrb5"
export boostIncludes="-I/usr/local/include/boost/"
export exrIncludes="-I/usr/local/include/OpenEXR/"
export botanIncludes="-I/opt/local/include/ -I./src"
export exrLibs="/usr/local/lib/libIlmImf.a /usr/local/lib/libHalf.a /usr/local/lib/libImath.a /usr/local/lib/libIlmThread.a /usr/local/lib/libIex.a"
#export curlLibs=`curl-config --libs`
export curlLibs="/usr/local/lib/libcurl.la -lssl -lcrypto -lz"
result=`fltk-config --use-gl --use-images --ldstaticflags`
export fltkLibs=${result}
echo "This is fltkLibs " $fltkLibs

result2=`flu-config --ldstaticflags`
echo "This is result2 " $result2
export fluFlags=${result2}
echo "This is fluFlags " $fluFlags

appName=JefeCheck$2.app
folderName=JefeCheck_OSX_$1
iconFilename=JefeCheck$2.icns


echo $appName
echo "Architecture " $1
#export architectureFlags="-arch $1"
#echo "*******USING ARCHITECTURE " $architectureFlags

./configure
make 

rm -Rf JefeCheck.app
mv src/jefecheck src/JefeCheck
strip src/JefeCheck
#flbundle -n -i src/checkmate1 -t MacBundleResources/JefeCheckLogo128.png -l MacBundleResources/JefeCheckLogo48.png -m MacBundleResources/JefeCheckLogo32.png -s MacBundleResources/JefeCheckLogo16.png -o ./JefeCheck.app
echo "creating bundle" $appName
flbundle -i src/JefeCheck -t MacResources/JefeCheckLogoRotatedScaled.png -o ./$appName

echo "copying icons"
cp MacResources/JefeCheckIcon.icns ./$appName/Contents/Resources/$iconFilename

mkdir ./$appName/Contents/Resources/FX
cp  ../../common/FX/*.jfx ./$appName/Contents/Resources/FX/
cp  ../../common/FX/*.frag ./$appName/Contents/Resources/FX/
cp  ../../common/FX/*.vert ./$appName/Contents/Resources/FX/
cp  ../../common/FX/*.tga ./$appName/Contents/Resources/FX/
cp  ../../common/FX/*.lut ./$appName/Contents/Resources/FX/
cp  ../../common/FX/*.cub ./$appName/Contents/Resources/FX/

cp  ../../common/Manual/JefeCheckManual.pdf ./$appName/Contents/Resources/
cp  ../../common/Manual/JefeCheckQuickStart.pdf ./$appName/Contents/Resources/

#copy the required dylibs
echo "copying libs from MacResources/"$1libs/lib/*.dylib
cp -vRf MacResources/$1libs/lib/*.dylib ./$appName/Contents/MacOS

#modify the executable and dylibs to point in the right direction
install_name_tool -change libgfl.3.90.dylib @executable_path/libgfl.3.90.dylib ./$appName/Contents/MacOS/JefeCheck
install_name_tool -change libgfle.3.90.dylib @executable_path/libgfle.3.90.dylib ./$appName/Contents/MacOS/JefeCheck
install_name_tool -change libgfl.3.90.dylib @executable_path/libgfl.3.90.dylib ./$appName/Contents/MacOS/libgfle.3.90.dylib
install_name_tool -id @executable_path/libgfl.3.90.dylib ./$appName/Contents/MacOS/libgfl.3.90.dylib 
#install_name_tool -id @executable_path/libcurl.4.dylib ./$appName/Contents/MacOS/libcurl.4.dylib

#cp MacBundleResources/prefs.ini ./JefeCheck.app/Contents/Resources/

mkdir ./$appName/Contents/Resources/Received

rm -Rf MacResources/$folderName
mkdir MacResources/$folderName
cp -R $appName MacResources/$folderName/$appName

echo $appName copied to MacResources/$folderName/$appName

rm -Rf ./$appName

#copy the correctLibs into the folderName> THIS IS NO LONGER NEEDED, WE BUNDLED THE DYLIBS IN THE APP USING install_name_tool
#echo "Copying libs"
#cp -Rf MacResources/$1libs/ MacResources/$folderName

echo "creating tarball"
#copy the installation guide into the foldername
cp MacResources/How_To_Install_JefeCheck.pdf MacResources/$folderName/
#create a tar from the whole MacResources/Architecture folder
cd MacResources
tar czf $folderName.tar.gz ./$folderName
echo "done, created " $folderName.tar.gz
echo "copying to installers folder..."
cp $folderName.tar.gz ../../../installers/
cd ..
#rm -Rf MacResources/$folderName


#MacResources/$folderName/$appName/Contents/MacOS/JefeCheck
open MacResources/$folderName/$appName