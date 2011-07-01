#ifndef GFCFXMANAGER_H
#define GFCFXMANAGER_H

#include "gfcfx.h"
#include <vector>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcFXManager{
public:
    gfcFXManager();

    ~gfcFXManager();
    
    void loadFX(std::string fileName);
    void deleteFX(int index);
    void deleteAllFX();
    gfcFX getFX(int index);
    gfcFX getFXbyHash(std::string hash);
    void setAutoLoad(int index, bool autoload);
    void autoLoadAll(bool autoload);
    void initWidgets();
    void saveScrollPosition();
    void restoreScrollPosition();
    
    void rebuildFXHashMap();
    
    std::vector<std::string> getAutoLoadPaths();
    std::vector<std::string> getMenuNames();
    std::vector<std::string> getHashes();
    std::map<std::string,int> getHashMap();
    
	//sorts the FXs alphabetically in the fxArray
	void sortFXs();

	int getFXIndexByName(std::string);
    
private:
std::vector<gfcFX> fxArray;
std::map<std::string, int> fxHashMap;
 //stores the position of each fx in the fxArray based on the md5Hash of each fx.

void fillLoadedScroll();

//GUI elements, maybe later abstarct into another class.
Fl_Scroll* loadedScroll; //this need to be initialized
Fl_Button* autoloadAllButton;
Fl_Progress* progress;

int scrollPosY;
int scrollPosX;

};


void fxManagerCB_OTHERS( Fl_Widget* o , void* v );
void fxManagerCB_DELETE( Fl_Widget* o , void* v );
void fxManagerCB_RELOAD( Fl_Widget* o , void* v );
void fxManagerCB_AUTOLOAD( Fl_Widget* o , void* v );
#endif
