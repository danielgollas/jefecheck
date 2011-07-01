#ifndef GFCFXSTACK_H
#define GFCFXSTACK_H

#include "gfcfx.h"
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
    void addFXGUIInfo(fxParamInfo theInfo,Fl_Widget* o);
    int handleGUICB(Fl_Widget* o, void *data);
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
std::map<Fl_Widget*,fxParamInfo> guiToFX; //maps each widget to info about the FX it belong to.
int numOfActiveFX;
};

#endif
