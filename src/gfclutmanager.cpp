#include "gfclutmanager.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "UIConstants.h"
#include "gfcStructures.h"

extern gfcSettings sett;


#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

gfcLUTManager lutManager;

gfcLUTManager::gfcLUTManager()
    : loadedScroll(nullptr),
      autoloadAllButton(nullptr),
      // The Qt build's Fl_Progress is a no-op stub class. Allocating a
      // single owned instance keeps loadLUT and the trilerp loaders
      // (which dereference `progress` on every status tick) safe to
      // call, without auditing every `progress->...` call site.
      progress(new Fl_Progress),
      scrollPosY(0),
      scrollPosX(0) {
}


gfcLUTManager::~gfcLUTManager() {
    // Owned in the Qt build; FLTK ones are owned by their parent group.
    delete progress;
}




void gfcLUTManager::setAutoLoad(int index, bool autoload) {
    if (index<lutArray.size()) {
        lutArray[index].autoload=autoload;
    } else {
        printf("Error, trying to set autoload on an out of range LUT \n");
    }
}



void gfcLUTManager::autoLoadAll(bool autoload) {
    //printf("Autoloading All!\n");

    for (int i=lutArray.size()-1;i>=0;i--)
        setAutoLoad(i,autoload);

    lutManager.saveScrollPosition();
    fillLoadedScroll();
    lutManager.restoreScrollPosition();
}


void gfcLUTManager::saveScrollPosition() {
}

void gfcLUTManager::restoreScrollPosition() {
}

void gfcLUTManager::deleteAllLUTs() {
    for (int i=lutArray.size()-1;i>=0;i--)
        deleteLUT(i);

    fillLoadedScroll();
}

void gfcLUTManager::loadLUT(std::string fileName) {

    //printf("trying to load LUT %s\n",fileName.c_str());
    if (fileName=="")
        return;
    //1. Check that this LUT or one with the same name is already loaded
    for ( int i=0;i<lutArray.size();i++ ) {
        if ( GetFilenameNoPath ( lutArray[i].filename ) ==GetFilenameNoPath ( fileName ) ) {
            // The Qt build runs without a real progress widget — calling
            // through a null pointer here was crashing on every LUT
            // autoload. Skip the status update; the load result still
            // returns cleanly via the `return` below.
            if (progress) {
                progress->color ( fl_rgb_color(42,42,0) );
                progress->copy_label ( " LUT Already Loaded, unload before reloading" );
            }
            return;
        }
    }
    //2. Load a LUT using a tmp CubeLUT
    CubeLUT tmpLut;
    int loadResult=0;
    loadResult=tmpLut.load ( fileName.c_str(),tmpLut.findMaximum(fileName.c_str(),progress),progress, 0,-1);

    //3. If succesfull, store the tmpLUT in the luts array, rebuild the LUT hash map and TODO: if connected, send the hash map to start a LUT Sync.
    if (loadResult!=0) {
        printf ( "Error loading LUT\n" );
        return;
    } else {
        lutArray.push_back ( tmpLut );
        //printf ( "LUT ARRAY SIZE=%i\n",lutArray.size() );
        rebuildLUTHashMap();
    }
    //4. Update the FX Manager Window to show the new FX
    fillLoadedScroll();
    //5. Update the FX Control Window to show the newly loaded FX in the add menu.
    //fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);
	//6. Update the main window's widgets
	plateManager.updateAllGUILUTWidgets();

}

void gfcLUTManager::rebuildLUTHashMap() {
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();
    int counter=0;


    lutHashMap.erase ( lutHashMap.begin(),lutHashMap.end() );
    for ( iter;iter!=end ;iter++ ) {
        //printf ( "Adding %i as (%s):%s\n",counter,iter->name, iter->md5Hash.c_str() );
        lutHashMap[iter->md5Hash]=counter;


        counter++;


    }

}

void gfcLUTManager::fillLoadedScroll() {
    if (!loadedScroll) {
        printf("NO GUI ASSIGNED TO LUT MANAGER!\n");
        return;
    }
}

void gfcLUTManager::initWidgets() {
}

void gfcLUTManager::deleteLUT(int index) {
    //1. Unload the lut by erasing it from the lut array.
    if (index<lutArray.size()) {
        lutArray[index].freeResources();
        lutArray.erase ( lutArray.begin() + index );
    } else {
        printf("Error, trying to unload an out of range LUT\n");
        return;
    }
    //2. Update the LUT Manager Window to not show the new FX
    lutManager.saveScrollPosition();
    fillLoadedScroll();
    lutManager.restoreScrollPosition();
    progress->value(0);
    progress->copy_label("LUT Unloaded");
    //4. Rebuild the hash map.
    rebuildLUTHashMap();
}

std::vector< std::string > gfcLUTManager::get3DLutNames() {
    std::vector<std::string> result;
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();
    for ( iter;iter!=end ;iter++ ) {
		if (iter->type==CubeLUT::BASELIGHT3DCUBE || iter->type==CubeLUT::IMAGELUT2D)
            result.push_back(iter->getNameNoPath());

    }
    return result;
}

std::vector< std::string > gfcLUTManager::get1DLutNames() {

    std::vector<std::string> result;
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();
    for ( iter;iter!=end ;iter++ ) {
        if (iter->type==CubeLUT::JEFECHECK1D)
            result.push_back(iter->getNameNoPath());

    }
    return result;
}

std::vector< std::string > gfcLUTManager::getAutoLoadPaths() {
    std::vector< std::string > result;
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();
    //fxHashMap.clear();
    //fxHashMap.erase ( fxHashMap.begin(),fxHashMap.end() );
    for ( iter;iter!=end ;iter++ ) {
        if (iter->autoload)
            result.push_back(iter->filename);
    }
    return result;
}

std::vector< std::string > gfcLUTManager::getAllNames() {
    std::vector<std::string> result;
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();
    for ( iter;iter!=end ;iter++ ) {
        result.push_back(iter->getNameNoPath());

    }
    return result;
}

CubeLUT gfcLUTManager::getLUT(int index) {
    if (index<lutArray.size() && index >=0 ) {
        return lutArray[index];
    } else {
        CubeLUT empty;
		
        //printf("Error, trying to get an out of range LUT %i\n",index);
        return empty;
    }
}




std::vector< std::string > gfcLUTManager::getHashes() {
    std::vector< std::string > result;
    std::vector<CubeLUT>::iterator iter=lutArray.begin(),end=lutArray.end();


    for ( iter;iter!=end ;iter++ ) {
        result.push_back(iter->md5Hash);
    }
    return result;
}

std::map< std::string, int > gfcLUTManager::getHashMap() {
    return lutHashMap;
}

CubeLUT gfcLUTManager::getLUTbyHash(std::string hash) {
    if ( lutHashMap.find ( hash ) !=lutHashMap.end() ) {
        return lutArray[lutHashMap[hash]];
    } else {
        CubeLUT tmpLUT;
        return tmpLUT;
    }
}


int gfcLUTManager::getLutIndexByName(std::string name) {


    for ( int i=0;i<lutArray.size();i++ ) {
        if ( name==lutArray[i].getNameNoPath() )  {
            //printf("Found the Correct LUT index at %i\n (%s)\n",i,lutArray[i].filename);
            return i;
            break;
        }
    }
    return -1; //we found no lut by that name
}

int gfcLUTManager::get1DLutIndexByName(std::string name) {
    int counter=0;
    for ( int i=0;i<lutArray.size();i++ ) {
		if (lutArray[i].type==CubeLUT::JEFECHECK1D) {
            if ( name==lutArray[i].getNameNoPath() )  {
                //printf("Found the Correct LUT index at %i\n (%s)\n",i,lutArray[i].filename);
                return counter;
            }
            counter++;
        }
    }
    return -1; //we found no 1D lut by that name

}

int gfcLUTManager::get3DLutIndexByName(std::string name) {
	int counter=0;
    for ( int i=0;i<lutArray.size();i++ ) {
        if (lutArray[i].type==CubeLUT::BASELIGHT3DCUBE || lutArray[i].type==CubeLUT::IMAGELUT2D) {
            if ( name==lutArray[i].getNameNoPath() )  {
                //printf("Found the Correct LUT index at %i\n (%s)\n",i,lutArray[i].filename);
                return counter;
            }
            counter++;
        }
    }
    return -1; //we found no 1D lut by that name
}

void gfcLUTManager::drawLut(int index, float scale, float rotX, float rotY, float rotZ, bool uniform ,int w, int h)
{
     if (index<lutArray.size()) {
     	if(uniform)
        lutArray[index].draw(scale, rotX, rotY, rotZ);
        else
        lutArray[index].drawSkewed(scale, rotX, rotY, rotZ);
    } else {
     
        //printf("Error, trying to get an out of range LUT\n");
     
    }
}
