#ifndef FXCONTROLWINDOW_H
#define FXCONTROLWINDOW_H

#include "gfcStructures.h"

#include <glad/glad.h>
#include "gfcfx.h"
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Menu_Bar.H>
//#include <FLU/Flu_Collapsable_Group.h>




#include <vector>
#include <map>
#include <string>

/**
@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

//defines a variable name belonging to a certaing effect.

void updateRecentlyLoadedStacks(std::string pfileName);

class gfcFX;
class gfcFXStack;

class fxParamInfo{
public:
	fxParamInfo(){};
	fxParamInfo(int pfxIndex,int pQuadrant,const char* pVariableName, const char* pGroupName, GFC_FX_GUI_TYPE  pType){
		fxIndex=pfxIndex;
		quadrant=pQuadrant;
		variableName=pVariableName;
		type=pType;
		groupName=pGroupName;
	};
	std::string groupName;
	int fxIndex;
	int quadrant;
	std::string variableName;
	GFC_FX_GUI_TYPE type;
};

//class gfcFX;

class FXControlWindow{
public:
	FXControlWindow();

	~FXControlWindow();

	void createAvailableFXMenu();
	Fl_Pack* createAppliedFXPane(int quadrant);
	//void createAppliedFXPaneOld(int quadrant);
	Fl_Group* createFX(gfcFX &theFX);
	Fl_Group* createGeneralControls(gfcFX &theFX);
	void createParticularControls(gfcFX &theFX,Fl_Group* theGroup);
	void createWindowOld(int pquadrant);
	void createWindow(int pquadrant);
	void updateWindow();
	void scheduleUpdateWindow(int quadrant);
	
	int quadrant;
	Fl_Double_Window *theWindow;
	Fl_Tile* tile;
	Fl_Box* bgBox;
	Fl_Menu_Bar* menuBar;
	Fl_Scroll *scroll;
	Fl_Scroll* bottomPaneScroll;
	Fl_Pack* fxPacker;
	Fl_Scroll* topPaneScroll;
	std::vector<Fl_Group*> activationGroup[4];
	//std::vector<Flu_Collapsable_Group*> groups[4];
	int prevScrollY[4];
	std::map<Fl_Widget*,fxParamInfo> guiToFX[4]; //from each GUI Widget, we can know what effect it belongs too and what name the variable has.
private:
	gfcFXStack* theStack;
	int fxCount;
	
	//used when updateWindow is called, and set when scheduleUpdateWindow is used.
	int updateWindowScheduled;
	int updateWindowQuadrant;
};

#endif
