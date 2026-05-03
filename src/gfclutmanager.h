#ifndef GFCLUTMANAGER_H
#define GFCLUTMANAGER_H

#include <vector>
#include <map>
#include "qt/qt_fltk_stubs.h"
#include "trilerp.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcLUTManager{
public:
    gfcLUTManager();

    ~gfcLUTManager();
    
    void loadLUT(std::string fileName);
    CubeLUT getLUT(int index);
    CubeLUT getLUTbyHash(std::string index);
    void setAutoLoad(int index, bool autoload);
    void autoLoadAll(bool autoload);
    void deleteLUT(int index);
    void deleteAllLUTs();
    void initWidgets();
    void saveScrollPosition();
    void restoreScrollPosition();
    void rebuildLUTHashMap();
    
    void drawLut(int index, float scale, float rotX, float rotY, float rotZ, bool uniform, int w, int h);
    
    int getLutIndexByName(std::string);
    int get1DLutIndexByName(std::string);
    int get3DLutIndexByName(std::string);
    
    std::vector<std::string> getAutoLoadPaths();
    std::vector<std::string> get3DLutNames();
    std::vector<std::string> get1DLutNames();
    std::vector<std::string> getAllNames();
    std::vector<std::string> getHashes();
    std::map<std::string,int> getHashMap();
    
    
    
    
private:
std::vector<CubeLUT> lutArray; //contains loaded LUTs
std::map<std::string, int> lutHashMap;
void fillLoadedScroll();
//GUI elements, maybe later abstarct into another class.
Fl_Scroll* loadedScroll; //this need to be initialized
Fl_Button* autoloadAllButton;
Fl_Progress* progress;

int scrollPosY;
int scrollPosX;
};

void lutManagerCB_OTHERS( Fl_Widget* o , void* v );
void lutManagerCB_DELETE( Fl_Widget* o , void* v );
void lutManagerCB_AUTOLOAD( Fl_Widget* o , void* v );

#endif
