#include "gfcfxmanager.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "UIConstants.h"
#include "gfcStructures.h"
extern gfcSettings sett;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

gfcFXManager fxManager;

gfcFXManager::gfcFXManager()
    : loadedScroll(nullptr),
      autoloadAllButton(nullptr),
      progress(nullptr),
      scrollPosY(0),
      scrollPosX(0) {
    // Same fix gfcLUTManager got in PR-21: zero-init the FLTK widget
    // pointers so the Qt build can call loadFX before initWidgets has
    // run. Each callsite below null-checks before dereferencing.
}


gfcFXManager::~gfcFXManager() {
}

/**
 * Loads an FX, stores it's information and will eventually notify the remoteManager to send a new FXhash message to start sinchronization.
 * @param fileName The name of the FX to load
 */
void gfcFXManager::loadFX(std::string fileName) {

    //printf("trying to load FX %s\n",fileName.c_str());
    if (fileName=="")
        return;

	//0. Load an FX using a tmp gfcFX
	gfcFX tmpFX;
	int loadResult=0;
	loadResult=tmpFX.load ( fileName.c_str(),0,progress );
	
	

    //1. Check that this FX or one with the same name is already loaded 
	//TODO: THIS CHECK SHOULD NOT ONLY BE BASED ON NAME, BUT ON THE HASH OF THE LUT
    for ( int i=0;i<fxArray.size();i++ ) {
        //if ( GetFilenameNoPath ( fxArray[i].filename ) ==GetFilenameNoPath ( fileName ) ) 
		if (  fxArray[i].md5Hash ==tmpFX.md5Hash)
		{
            if (progress) {
                progress->color ( fl_rgb_color(42,42,0) );
                progress->copy_label ( "Already Loaded, unload before reloading" );
            }
            printf("Already Loaded, unload before reloading\n");
            return;
        }
    }
    
    
    //2. If one with the same name already exists, unload and continue
	for ( int i=0;i<fxArray.size();i++ ) {
		if ( GetFilenameNoPath ( fxArray[i].filename ) ==GetFilenameNoPath ( fileName ) ) 
		{
			printf("An FX with this name was already loaded (%s), overwritng previous one\n",GetFilenameNoPath(fileName).c_str());
			int index = i;
			if (index<=fxArray.size()) {
				fxArray[index].freeResources();
				fxArray.erase ( fxArray.begin() + index );
			} else {
				printf("Error, trying to unload an out of range FX\n");
			}
			rebuildFXHashMap();
			break;
		}
	}
	


    //3. If succesfull, store the tmpFX in the fxs array, rebuild the FX hash map.
    if (loadResult!=0) {
        printf ( "Error loading FX\n" );
        return;
    } else {
        fxArray.push_back ( tmpFX );
        //printf ( "FX ARRAY SIZE=%i\n",fxArray.size() );
		sortFXs();
		//the hash map is rebuilt in the sortFXs method.
		//rebuildFXHashMap(); 
    }
    //4. Update the FX Manager Window to show the new FX
    fillLoadedScroll();
}

void gfcFXManager::deleteFX(int index) {
    //1. Unload the FX by erasing it from the fxs array.
    if (index<=fxArray.size()) {
        fxArray[index].freeResources();
        fxArray.erase ( fxArray.begin() + index );
    } else {
        printf("Error, trying to unload an out of range FX\n");
        return;
    }
    //2. Update the FX Manager Window to not show the new FX
    fxManager.saveScrollPosition();
    fillLoadedScroll();
    fxManager.restoreScrollPosition();
    if (progress) {
        progress->value(0);
        progress->copy_label("FX Unloaded");
    }
    //3. Update the FX Control Window to not show the newly loaded FX in the add menu.
    //TODO: DO this
    //4. Rebuild the hash map.
	sortFXs();
	//theHashmap is rebuilt in the sortFXs method
    //rebuildFXHashMap();
}

void gfcFXManager::setAutoLoad(int index, bool autoload) {
    //1.Mark the FX as autoload or not according to autoload parameter
    if (index<fxArray.size()) {
        fxArray[index].autoload=autoload;
    } else {
        printf("Error, trying to set autoload on an out of range FX \n");
    }
}

/**
 * Fills the FX Manager's window scroll group with the loaded FXs, it creates the buttons and assigns them appropiate callbacks, those callbacks will usually receive the FXs index in the fxs array as a parameter to know what FX is being manipulated.
 */
void gfcFXManager::fillLoadedScroll() {
    if (!loadedScroll) {
        printf("NO GUI ASSIGNED TO FX MANAGER!\n");
        return;
    }
}


void gfcFXManager::autoLoadAll(bool autoload) {
    printf("Autoloading All!\n");

    for (int i=fxArray.size()-1;i>=0;i--)
        setAutoLoad(i,autoload);

    fxManager.saveScrollPosition();
    fillLoadedScroll();
    fxManager.restoreScrollPosition();
}

void gfcFXManager::initWidgets() {
}

void gfcFXManager::saveScrollPosition() {
    scrollPosX=loadedScroll->xposition();
    scrollPosY=loadedScroll->yposition();

}

void gfcFXManager::restoreScrollPosition() {
    //loadedScroll->position(scrollPosX,scrollPosY);
	loadedScroll->scroll_to(scrollPosX,scrollPosY);
}

void gfcFXManager::deleteAllFX() {
    for (int i=fxArray.size()-1;i>=0;i--)
        deleteFX(i);

    //fxManager.saveScrollPosition();
    fillLoadedScroll();
    //fxManager.restoreScrollPosition();
}

void gfcFXManager::rebuildFXHashMap() {
    std::vector<gfcFX>::iterator iter=fxArray.begin(),end=fxArray.end();
    int counter=0;

    //fxHashMap.clear();
    fxHashMap.erase ( fxHashMap.begin(),fxHashMap.end() );
    for ( iter;iter!=end ;iter++ ) {
        //printf ( "Adding %i as (%s):%s\n",counter,iter->name, iter->md5Hash.c_str() );
        fxHashMap[iter->md5Hash]=counter;
        counter++;
    }

	//just a test, move the last fx to the first;

	gfcFX tmpFX;
	/*if (fxArray.size()>0)
	{
		tmpFX=fxArray[fxArray.size()-1];
		printf("tmpFX=%s\n",tmpFX.menuName.c_str());
		fxArray[fxArray.size()-1]=fxArray[0];
		printf("fxArray[fxArray.size()-1]=%s\n",fxArray[fxArray.size()-1].menuName.c_str());
		fxArray[0]=tmpFX;
		printf("fxArray[0]=%s\n",fxArray[0].menuName.c_str());
	}*/
	
	
}

std::vector< std::string > gfcFXManager::getAutoLoadPaths() {
    std::vector< std::string > result;
    std::vector<gfcFX>::iterator iter=fxArray.begin(),end=fxArray.end();
    //fxHashMap.clear();
   
    for ( iter;iter!=end ;iter++ ) {
        if (iter->autoload)
            result.push_back(iter->filename);
    }
    return result;
}

bool fxGreaterThan(const gfcFX elem1, const gfcFX elem2)
{
	/*bool returnValue=false;
	if(strcmp(elem1.menuName.c_str(), elem2.menuName.c_str())>0)
	{
		printf("  %s > %s\n",elem1.menuName.c_str(),elem2.menuName.c_str());
		returnValue=true;
	}
	else
	{
		printf("  %s < %s\n",elem1.menuName.c_str(),elem2.menuName.c_str());
		returnValue= false;
	}
	
	return returnValue;*/
	return elem1.menuName<elem2.menuName;
}

void gfcFXManager::sortFXs()
{
	std::sort(fxArray.begin(),fxArray.end(),fxGreaterThan);
	/*std::cout << "*******sorted fxArray:************\n";
	for (int i=0; i<fxArray.size();i++)
	{
		std::cout << fxArray[i].menuName<< std::endl;
	}*/
	
	rebuildFXHashMap();

	/*std::cout << "----------sorted fxArray after rebuildFXHasMap:----------\n";
	for (int i=0; i<fxArray.size();i++)
	{
		std::cout << fxArray[i].menuName<< std::endl;
	}*/

}

int gfcFXManager::getFXIndexByName(std::string pname)
{
	std::vector<gfcFX>::iterator iter=fxArray.begin(),end=fxArray.end();
	int result=0;
	for ( iter;iter!=end; iter++ ) {
		
		if (iter->name==pname)
		{
			return result;
		}	
		result++;
	}

	//did not find it...
	return -1;
}

/**
 * Gets the menu names for all the available FXs
 * @return
 */
std::vector< std::string > gfcFXManager::getMenuNames() {
    std::vector< std::string > result;
    std::vector<gfcFX>::iterator iter=fxArray.begin(),end=fxArray.end();

    
    for ( iter;iter!=end ;iter++ ) {
        result.push_back(iter->menuName);
    }
    return result;
}

/**
 * Gets the MD5 Hashes for all the available FXs
 * @return 
 */
std::vector< std::string > gfcFXManager::getHashes()
{
    std::vector< std::string > result;
    std::vector<gfcFX>::iterator iter=fxArray.begin(),end=fxArray.end();

    
    for ( iter;iter!=end ;iter++ ) {
        result.push_back(iter->md5Hash);
    }
    return result;
}

gfcFX gfcFXManager::getFX(int index)
{
	if (index<=fxArray.size()) {
        return fxArray[index];
    } else {
    	gfcFX empty;
        printf("Error, trying to get an out of range FX\n");
        return empty;
    }
}

std::map< std::string, int > gfcFXManager::getHashMap()
{	
	//printf("I will be returning a map with %i members\n",fxHashMap.size());
	return fxHashMap;
}

gfcFX gfcFXManager::getFXbyHash(std::string hash)
{	
	
	if ( fxHashMap.find ( hash ) !=fxHashMap.end() ) {//the FX is here, return it
                    return fxArray[fxHashMap[hash]];
        }
        else{
        gfcFX tmpFX;
		tmpFX.name="";
        return tmpFX;
        }
}


