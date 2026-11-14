#ifndef GFCFXSTACK_H
#define GFCFXSTACK_H

#include "gfcfx.h"
#include <cstdint>
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
    // Appends theFX and returns its instance id: the FX's own id when it is
    // non-zero and not already used in this stack, otherwise a fresh one.
    // Ids are never reused within a stack's life, even after a remove or clear.
    std::uint32_t addFX(gfcFX theFX);
    // Stack index of the FX with this instance id; -1 when absent (or id 0).
    int indexOfInstance(std::uint32_t id) const;
    gfcFX getFX(int index);
    gfcFX& fxAt(int index);
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

    // Returns the GUI type of a widget (FX_GUI_FLOAT / BOOL / CHOICE /
    // TEXTURE / LUT / CUBE), needed to decide which field a network attrib
    // message carries. Returns FX_GUI_UNKNOWN when the widget doesn't exist.
    GFC_FX_GUI_TYPE getWidgetType(int fxIndex,
                                  const std::string& groupName,
                                  const std::string& widgetName) const;

    int getNumOfFXs();
    int getNumOfActiveFXs();

    // Sets the active flag on the FX at fxIndex. Bounds-checked; no-op
    // when the index is out of range. Keeps the vestigial numOfActiveFX
    // member consistent (getNumOfActiveFXs recomputes by iterating, so
    // the member is only kept in sync so other code paths that read it
    // don't drift).
    void setActive(int fxIndex, bool active);

    // Reorders the stack by moving the FX at `from` to position `to`
    // (erase + insert semantics, NOT an adjacent swap — moving item 0 to
    // position 3 lands it after the others). No-op when either index is
    // out of range or they are equal.
    void moveFX(int from, int to);
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
std::uint32_t nextInstanceId_ = 1; // always above every id in fxs
};

#endif
