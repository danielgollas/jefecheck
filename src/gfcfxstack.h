#ifndef GFCFXSTACK_H
#define GFCFXSTACK_H

#include "gfcfx.h"
#include <map>
#include <vector>
#include "gfcNetworkStructures.h"
#include "xmlParser.h"

// Defined in fxcontrolwindow.h in the FLTK build; redefined here for the
// Qt build, which never pulls fxcontrolwindow.h.
#include <string>
class fxParamInfo {
public:
    fxParamInfo() {}
    fxParamInfo(int pfxIndex, int pQuadrant, const char* pVariableName,
                const char* pGroupName, GFC_FX_GUI_TYPE pType)
        : groupName(pGroupName), fxIndex(pfxIndex), quadrant(pQuadrant),
          variableName(pVariableName), type(pType) {}
    std::string groupName;
    int fxIndex;
    int quadrant;
    std::string variableName;
    GFC_FX_GUI_TYPE type;
};
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

    // Direct widget-value setter, mirroring processNetFXAttribInfo's
    // mutation but without the network struct round-trip. The Qt FX
    // param panel calls this to push slider / combo / checkbox edits
    // back into gfcFX's group.widgets[name].value, which gfcFX::bind
    // reads when the next frame composites. No-op when fxIndex is
    // out of range, the group doesn't exist, or the widget doesn't.
    void setWidgetValue(int fxIndex,
                        const std::string& groupName,
                        const std::string& widgetName,
                        float value);
    
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
