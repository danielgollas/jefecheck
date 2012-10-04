echo "-------------------------------"
echo "Installing JefeCheck"
echo "-------------------------------"
echo "Creating Folder Structure for JefeCheck"
mkdir ~/.JefeCorp
mkdir ~/.JefeCorp/JefeCheck
mkdir ~/.JefeCorp/JefeCheck/JefeCheck
mkdir ~/.JefeCorp/JefeCheck/JefeCheck/Received
mkdir ~/.JefeCorp/JefeCheck/JefeCheck/FX
echo "Copying FXs and LUTs"
cp  ./FX/* ~/.JefeCorp/JefeCheck/JefeCheck/FX
echo "Copying User Guide and QuickStart guide"
cp  ./*.pdf ~/.JefeCorp/JefeCheck/JefeCheck/
#cp -v prefs.ini  ~/.JefeCorp/JefeCheck/JefeCheck/
#tree ~/.JefeCorp/JefeCheck/
echo "-------------------------------"
echo "Done Installing JefeCheck"
echo "-------------------------------"
