#ifndef GFCFXSTACK_H
#define GFCFXSTACK_H

#include "gfcfx.h"
#include <map>
#include <vector>
#include "gfcNetworkStructures.h"
#include "xmlParser.h"

class fxParamInfo;
struct gfcNetFXAttribInfo;
struct gfcNetFXCommonInfo;

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcFXStack{
public:
    gfcFXStack();

    ~gfcFXStack();
    
    public:
    void clearStack();
    void addFX(gfcFX theFX);
    gfcFX getFX(int index);
    std::vector< gfcFX > getAllFXs();
    
    int getNumOfFXs();
    int getNumOfActiveFXs();
    // FX-control widget callbacks. The handle is opaque so the model class
    // doesn't pull a UI toolkit's headers; in the FLTK build it's an
    // Fl_Widget*, and in the Qt build it'll be the corresponding QWidget*.
    // handleGUICB is FLTK-specific today and is a no-op outside USE_FLTK.
    void addFXGUIInfo(fxParamInfo theInfo, void* widgetHandle);
    int handleGUICB(void* widgetHandle, void* data);
    void processNetFXAttribInfo(gfcNetFXAttribInfo &info);
    void processNetFXCommonInfo(gfcNetFXCommonInfo &info);
    void saveStackToNode(XMLNode &pnode) const;
    void saveStackToFile(std::string filename) const;
    
    void appendFXStack(gfcFXStack otherStack);
        
    std::vector<std::string> loadStackFromNode(XMLNode &pnode,int *FXSi=NULL);
    std::vector<std::string> loadStackFromFile(std::string filename);
	std::vector<std::string> loadStackFromString(std::string s);
	
    
    std::vector<int> getActiveFXIndexes();
    
    std::string getDescriptionString();
    
private:
std::vector<gfcFX> fxs;
std::map<void*, fxParamInfo> guiToFX; // map keyed by opaque widget handle (see API doc).
int numOfActiveFX;
};

#endif
