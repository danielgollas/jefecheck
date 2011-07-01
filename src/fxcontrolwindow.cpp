#ifdef WIN32
#include <windows.h>
#endif

#include "fxcontrolwindow.h"
#include "fxWindow.h"
#include <string>
#include <map>
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Choice.H>
#include <FLU/Flu_Collapsable_Group.h>
#include <FLU/Flu_Simple_Group.h>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_File_Chooser.H>
#include "gfcfx.h"
#include "trilerp.h"
#include "xmlParser.h"
#include "gfcStructures.h"
//#include "network.h"
#include "Fl_Button_gfc.h"
#include "Fl_Choice_gfc.h"
 
#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "lutWindow.h"
extern LutWindow lutw;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "mainWindow.h"
extern MainWindow mw;

extern gfcSettings sett;
extern Fl_File_Chooser *fc;
extern FXControlWindow fxControlWindow1;
extern FXWindow fxw;
extern void fxCBFillLoadedScroll();
//extern std::vector<CubeLUT> lutArray;
//extern std::vector<gfcFX> fxArray;
//extern std::vector<int> fxArrayActiveCount;
//extern std::vector<gfcFX> fxApplied[4]; // this are the applied fx for each quadrant, they can change order and are added when the correponding button on the FX theWindow is pressed.
extern void save_input_file(Fl_File_Chooser *w, void *userdata);
extern char gFilename[300];
extern int numberOfActiveEffects[4];
extern bool gNetworkFXAddEvent;
extern std::map<std::string, int> fxHashMap;
int groupOffset=0;
int controlSize=15;
int controlCount=0;
int titleOffset=80;


int floatsOffset=0;
int floatsAmount=2;
int floatsSeparation=15;
int floatsSize=15;

int texturesOffset=0;
int texturesAmount=2;
int texturesSeparation=15;
int texturesSize=15;

int cubesOffset=0;
int cubesAmount=3;
int cubesSeparation=15;
int cubesSize=15;

int boolsOffset=0;
int boolsAmount=5;
int boolsSeparation=15;
int boolsSize=15;

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

FXControlWindow::FXControlWindow() {}


FXControlWindow::~FXControlWindow() {}



void updateRecentlyLoadedStacks(std::string pfileName) {
    if (sett.recentFXStacks.size()<sett.maxRecentFXStacks) {
        //check if the stack is not in the vector already.
        bool alreadyInRecent=false;
        for (int i=0;i<sett.recentFXStacks.size();i++) {
            if (sett.recentFXStacks[i]==pfileName) {
                //it's already in the recent stack, remove it from it's position and push it to the back (which is the first to appear in menus)
                sett.recentFXStacks.erase(sett.recentFXStacks.begin()+i);
                break;
            }
        }
        
            sett.recentFXStacks.push_back(pfileName);
            
    } else {
        {
            bool alreadyInRecent=false;
            for (int i=0;i<sett.recentFXStacks.size();i++) {
                if (sett.recentFXStacks[i]==pfileName) { //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
                    alreadyInRecent=true;
                    sett.recentFXStacks.erase(sett.recentFXStacks.begin()+i);
                    sett.recentFXStacks.push_back(pfileName);
                    break;
                }
            }

            if (!alreadyInRecent) { //if it's not in the recent, then erase the first one and push the new one.
                sett.recentFXStacks.erase(sett.recentFXStacks.begin());
                sett.recentFXStacks.push_back(pfileName);
            }
        }
    }
}

void fxMenuCB(Fl_Widget* o, void* data) {
    //printf("FX MENU CALLBACK!!!!!!\n");
    int quadrant=fxControlWindow1.quadrant;
    //0 and negative values indicate the index of an FX in the fxArray. If the data is negative, then it means that we should add the abs of that index from the fxArray into the quadrants applied array.
    if ((long)data<=0) {
        int fxIndex=-(long)data;
        gfcFX tmpFX=fxManager.getFX(fxIndex);
	printf("adding fx %s.\n",tmpFX.name.c_str());
        plateManager.addFXToPlate(quadrant,tmpFX);
    }

    else {//manage the other menus under control/
        switch ((long)data) {
        
        case FX_MENU_SAVE_FAVORITES_0:
        case FX_MENU_SAVE_FAVORITES_1:
        case FX_MENU_SAVE_FAVORITES_2:
        case FX_MENU_SAVE_FAVORITES_3:
        case FX_MENU_SAVE_FAVORITES_4:
        {
        	long whichOne=(long)data-FX_MENU_SAVE_FAVORITES_0;
        	plateManager.saveFavoriteFromPlate(whichOne);
        	printf("Save to Favorite %i\n",whichOne);
        }
        break;
        
        case FX_MENU_APPEND_FAVORITES_0:
        case FX_MENU_APPEND_FAVORITES_1:
        case FX_MENU_APPEND_FAVORITES_2:
        case FX_MENU_APPEND_FAVORITES_3:
        case FX_MENU_APPEND_FAVORITES_4:
        {
        	long whichOne=(long)data-FX_MENU_APPEND_FAVORITES_0;
        	printf("Append Favorite %i\n",whichOne);
        	plateManager.appendFavoriteOnPlate(whichOne);
        }
        break;
        
        case FX_MENU_LOAD_FAVORITES_0:
        case FX_MENU_LOAD_FAVORITES_1:
        case FX_MENU_LOAD_FAVORITES_2:
        case FX_MENU_LOAD_FAVORITES_3:
        case FX_MENU_LOAD_FAVORITES_4:
        {
        	long whichOne=(long)data-FX_MENU_LOAD_FAVORITES_0;
        	printf("Load Favorite %i\n",whichOne);
        	plateManager.setFavoriteOnPlate(whichOne);
        }
        break;
        
        
        
        
        case FX_MENU_CLOSE:
            //printf("Close the FX Window\n");
            fxControlWindow1.theWindow->hide();
            break;

        case FX_MENU_RECENT: {
            //printf("menu clicked: %s\n",((Fl_Menu_*)o)->text());
            /*fxControlWindow1.loadStack(((Fl_Menu_*)o)->text());
            fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);*/

            std::string pfileName=((Fl_Menu_*)o)->text();
            RemoveMenuSlash(pfileName);
            plateManager.loadStackFromFile(quadrant,pfileName);
            /*updateRecentlyLoadedStacks(pfileName);*/
            /*fxControlWindow1.scheduleUpdateWindow(quadrant);*/
        }
        break;

        case FX_MENU_LOAD_STACK: {

            fc->callback(save_input_file);
            fc->preview(0);
            fc->filter("FX Stacks (*.fxs)");
            fc->label("Select an FX Stack File");
            fc->type(Fl_File_Chooser::SINGLE);
            fc->show();
            while (fc->shown())
                Fl::wait();

            if (fc->count()) {

                std::string pfileName=fc->value(0);
                plateManager.loadStackFromFile(quadrant,pfileName);
                /*updateRecentlyLoadedStacks(pfileName);
                fxControlWindow1.scheduleUpdateWindow(quadrant);*/
            }

        }
        break;

        case FX_MENU_SAVE_STACK: {
            fc->callback(save_input_file);
            fc->preview(0);
            fc->filter("FX Stacks (*.fxs)");
            fc->label("Select or Create an FX Stack File");
            fc->type(Fl_File_Chooser::CREATE);
            fc->show();
            while (fc->shown())
                Fl::wait();

            if (fc->count()) {
                plateManager.saveStackToFile(quadrant,fc->value(0));
                updateRecentlyLoadedStacks(fc->value(0));
                fxControlWindow1.scheduleUpdateWindow(quadrant);
            }
        }
        break;

        case FX_MENU_CLEAR_ALL:
            //printf("Clear All FX for quadrant %i\n",fxControlWindow1.quadrant);
            /* fxApplied[fxControlWindow1.quadrant].clear();
            numberOfActiveEffects[fxControlWindow1.quadrant]=0;
            fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);*/
            plateManager.clearFXStack(quadrant);
            fxControlWindow1.scheduleUpdateWindow(quadrant);
            break;

        case FX_MENU_LUT_MANAGER:
            lutw.lutWindow->show();
            break;

        case FX_MENU_MANAGER:
            fxw.fxWindow->show();
            break;
        }
    }

}


void FXControlWindow::createWindow(int pquadrant) {


    char tmpName[30];

    quadrant=pquadrant;

    sprintf(tmpName,"FX Controls Q%i\n",pquadrant+1);
    {
        {
            int x, y;
            x=mw.mainWindow->x();
            y=mw.mainWindow->y();
            Fl_Double_Window* o = theWindow= new Fl_Double_Window(x, y, 355, 600, tmpName);
            o->copy_label(tmpName);

            //theWindow->clear_border();
            theWindow->set_non_modal();
            theWindow->resizable(theWindow);
            theWindow->box(FL_FLAT_BOX);
            theWindow->box(FL_THIN_UP_FRAME);
            theWindow->color(FL_BLACK);
            o->user_data((void*)(this));

            {
                {
                    Fl_Menu_Bar* o = menuBar = new Fl_Menu_Bar(0, 0, theWindow->w(), 25);
                    o->box(FL_FLAT_BOX);
                    o->color(fl_rgb_color(GFC_BG_COLOR,GFC_BG_COLOR,GFC_BG_COLOR));
                    o->labelcolor(FL_BACKGROUND2_COLOR);
                    o->textcolor(7);
                    //o->box(FL_FLAT_BOX);

                    //Control FX Manager

                    o->add
                    ("Control/FX Manager...",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_MANAGER,0);
                    o->add
                    ("Control/LUT Manager...",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_LUT_MANAGER,0);
                    o->add
                    ("Control/Clear All",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_CLEAR_ALL,0);
                    o->add
                    ("Control/Close",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_CLOSE,0);
                }

                bottomPaneScroll=new Fl_Scroll(0,menuBar->h(),theWindow->w(),theWindow->h()-menuBar->h());
                {
                    bottomPaneScroll->box(FL_FLAT_BOX);
                    //bottomPaneScroll->color(FL_BLACK);
                    bottomPaneScroll->color(fl_rgb_color(GFC_BG_COLOR,GFC_BG_COLOR,GFC_BG_COLOR));
                }
                bottomPaneScroll->end();

                bottomPaneScroll->redraw();
                Fl::check();
                theWindow->resizable(bottomPaneScroll);

            }
            theWindow->end(); //theWindow end
        }
    }
    scheduleUpdateWindow(0);
}

void generalFXGUICB(Fl_Widget* o, void* data) {
    //printf("FX GUI CALLBACK!!!!!!\nQuadrant: %i\n",((FXControlWindow*)data)->quadrant);

    FXControlWindow* cw=((FXControlWindow*)data);

    if (plateManager.handleFXGUICB(fxControlWindow1.quadrant, o, data))
        cw->scheduleUpdateWindow(fxControlWindow1.quadrant);

    Fl::check();
}

void FXControlWindow::updateWindow() { //clears the scroll, creates the applied FXs guis, adds the available FXs to the menu,  and sets the title, does not destroy the control theWindow.

    if (!this->theWindow->visible())
        return;

	
    if(updateWindowScheduled){
    int pquadrant=updateWindowQuadrant;
    updateWindowScheduled=0;
    menuBar->clear();
    //delete menuBar;

    createAvailableFXMenu();

    menuBar->redraw();

    //prevScrollY[quadrant]=bottomPaneScroll->yposition();
    //int prevScrollX=bottomPaneScroll->xposition();

    bottomPaneScroll->clear();

    //bottomPaneScroll->position(0,-menuBar->h());
    bottomPaneScroll->add(createAppliedFXPane(pquadrant));

    char tmpName[256];
    //bottomPaneScroll->h(fxPacker->h()-menuBar->h());
    sprintf(tmpName,"FX Controls - Plate %i",pquadrant+1);
    theWindow->copy_label(tmpName);
    bottomPaneScroll->init_sizes();
    bottomPaneScroll->redraw();
    }
}

Fl_Group* FXControlWindow::createGeneralControls(gfcFX &theFX) {
    Fl_Pack *generalControlPack=new Fl_Pack(5, 20,10,20,"");
    generalControlPack->type(Fl_Pack::HORIZONTAL);
    generalControlPack->spacing(5);
    generalControlPack->end();
    {
        Fl_Check_Button* o = new Fl_Check_Button(0,0, 20, 20);

        o->copy_label("On/Off");
        o->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
        o->labelfont(FL_HELVETICA_BOLD);
		o->box(FL_NO_BOX);
		o->down_box(FL_FLAT_BOX);
		o->color(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
		o->selection_color(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));

        o->align(FL_ALIGN_RIGHT);
        o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
        fxParamInfo info(fxCount,quadrant,"NO USE: ACTIVATE OR NOT","NOGROUP",FX_ACTIVATE);
        theStack->addFXGUIInfo(info,o);
        o->value(theFX.active);
        generalControlPack->add(o);
        int w=0,h=0;

        //add this dummy to protect the on/off label size.
        fl_font(o->labelfont(), o->labelsize());
        Fl_Group* onOffDummy = new Fl_Group(0, 0, fl_width(o->label()), 1);
        onOffDummy->end();
        generalControlPack->add(onOffDummy);
    }
    {
        //Fl_Button* o = new Fl_Button(groupOffsetX+40,groupOffsetY, 45, 15, "Delete");
        Fl_Button_gfc* o = new Fl_Button_gfc(0,0, 45, 15, "Delete");
        o->color(FL_BLACK);
        o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
        o->labelcolor(FL_RED);
        fxParamInfo info(fxCount,quadrant,"NO USE: DOWN","NOGROUP", FX_DELETE);
        theStack->addFXGUIInfo(info,o);
        generalControlPack->add(o);
    }
    {
        //                            Fl_Button* o = new Fl_Button(groupOffsetX+40,groupOffsetY, 45, 15, "Reset");
        Fl_Button_gfc* o = new Fl_Button_gfc(0,0, 45, 15, "Reset");
        o->color(FL_BLACK);
        o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
        fxParamInfo info(fxCount,quadrant,"NO USE: DOWN","NOGROUP", FX_RESET);
        theStack->addFXGUIInfo(info,o);
        generalControlPack->add(o);
    }
    {
        Fl_Button_gfc* o = new Fl_Button_gfc(0,0, 45, 20, "@8>");
        o->color(FL_BLACK);
		o->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
        o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
        fxParamInfo info(fxCount,quadrant,"NO USE: MOVE UP","NOGROUP",FX_MOVEUP);
        theStack->addFXGUIInfo(info,o);
        generalControlPack->add(o);

    }
    {
        Fl_Button_gfc* o = new Fl_Button_gfc(0,0, 45, 20, "@2>");
        o->color(FL_BLACK);
		o->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
        o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
        fxParamInfo info(fxCount,quadrant,"NO USE: MOVE DOWN","NOGROUP",FX_MOVEDOWN);
        theStack->addFXGUIInfo(info,o);
        generalControlPack->add(o);
    }
    return generalControlPack;
    //****************END GENERAL CONTROLS***************************
}

void FXControlWindow::createParticularControls(gfcFX &theFX,Fl_Group* theGroup) {
    //****************WIDGET GROUPS***************************
    //iterate through the groups of widgets
    std::map<std::string, gfcFXWidgetGroup>::iterator groupsIter=theFX.groups.begin();
    std::map<std::string, gfcFXWidgetGroup>::iterator groupsIterEnd=theFX.groups.end();
    int controlFontSize=10;
	int controlTextSize=10;
    for (groupsIter;groupsIter!=groupsIterEnd;groupsIter++) {
        //pack this group into a vertical pack
        int theSizeY=10;
#ifdef __APPLE__
        //theSizeY=theFX.sizeY;
#endif
        //printf("theSizeY=%i\n",theSizeY);
        Fl_Pack *widgetGroup=new Fl_Pack(0, 0,theGroup->w(),theSizeY,groupsIter->second.name.c_str());
        widgetGroup->copy_label(groupsIter->second.name.c_str());
        widgetGroup->labelcolor(fl_rgb_color(128,128,128));
        widgetGroup->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
        widgetGroup->labelsize(12);
        widgetGroup->labelfont(FL_HELVETICA);
        //widgetGroup->box(FL_THIN_UP_FRAME);
        widgetGroup->end();


        //widgetGroupDummy adds space for the widget group label

        Fl_Group* widgetGroupDummy = new Fl_Group(0, 0, widgetGroup->w(), widgetGroup->labelsize()+5);
        widgetGroupDummy->end();
        widgetGroup->init_sizes();
        widgetGroup->redraw();
        Fl::check();
        theGroup->add(widgetGroupDummy);
        theGroup->add(widgetGroup);

        //**********INDIVIDUAL CONTROLS*************/
        {
            //vertical pack to pack all the rows of widgets, with horizontal packs to pack each widget in a row.
            Fl_Pack *vPack=new Fl_Pack(0,0,widgetGroup->w(),1);
            //hPack->box(FL_FLAT_BOX);
            //hPack->color(FL_RED);
            fl_font(fl_font(), controlFontSize);
            vPack->spacing(fl_height()+2);
            vPack->end();

            Fl_Group* implicitNewLine = new Fl_Group(0, 0, 1, 1);
            implicitNewLine->end();
            vPack->add(implicitNewLine);

            //to iterate through each widget, we don't iterate the widgets map, but the widgetsOrder vector to acces the widget directly inside the map, since the order is of important.
            std::vector<std::string>::iterator widgetIter=groupsIter->second.widgetsOrder.begin();
            std::vector<std::string>::iterator widgetIterEnd=groupsIter->second.widgetsOrder.end();
            Fl_Pack *hPack=NULL;
            //for (widgetIter;/*widgetIter*/widgetIterEnd!=widgetIterEnd;widgetIter++) {
            for (widgetIter;widgetIter!=widgetIterEnd;widgetIter++) {
                gfcFXWidget tmpWidget=groupsIter->second.widgets[*widgetIter]; //get the actual widget from the map using the name from the orders vector
                switch (tmpWidget.type) {
                case FX_GUI_NEWLINE:
                    if (hPack!=NULL) {
                        hPack->end();
                        //theGroup->add(hPack);
                        vPack->add(hPack);
                    }

                    hPack = new Fl_Pack(0,0,0,20);
                    hPack->type(Fl_Pack::HORIZONTAL);
                    hPack->spacing(15);
                    hPack->end();

                    break;

                case FX_GUI_FLOAT: {
                    Fl_Value_Input *o = new Fl_Value_Input(0,0,60,0,tmpWidget.label.c_str());
                    o->copy_label(o->label());
                    o->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
					o->textsize(controlTextSize);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    o->step(tmpWidget.step);
                    o->maximum(tmpWidget.maximum);
                    o->minimum(tmpWidget.minimum);
                    o->value(tmpWidget.value);
					o->box(FL_FLAT_BOX);
					o->color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));
					o->textcolor(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));
                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_FLOAT);

                    theStack->addFXGUIInfo(info,o);
                    hPack->add(o);
                }
                break;

                case FX_GUI_BOOL: {
                    Fl_Check_Button *o = new Fl_Check_Button(0,0,20,20,tmpWidget.label.c_str());
                    o->align(FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    o->value(tmpWidget.value);
                    o->copy_label(o->label());
					
					o->box(FL_NO_BOX);
					o->down_box(FL_FLAT_BOX);
					o->color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));
					o->selection_color(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));


                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_BOOL);
                    theStack->addFXGUIInfo(info,o);

                    //add a spacer before each bool to account for it's left aligned label.
                    Fl_Group* boolDummy = new Fl_Group(0, 0, max((fl_width(o->label())-hPack->spacing()),0), 1);
                    boolDummy->end();
                    hPack->add(boolDummy);
                    hPack->add(o);

                }
                break;

                case FX_GUI_SPACER: {

                    Fl_Box *o = new Fl_Box(0,0,tmpWidget.width,1,"");
                    o->box(FL_NO_BOX);
                    o->align(FL_ALIGN_INSIDE);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    hPack->add(o);
                }
                break;

                case FX_GUI_CHOICE: {

                    Fl_Choice_gfc* o=new Fl_Choice_gfc(0,0,75,20,tmpWidget.label.c_str());
                    o->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
					o->textsize(controlTextSize);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    o->copy_label(o->label());
                    for (int optionsCounter=0;optionsCounter<tmpWidget.options.size();optionsCounter++) {
                        o->add(tmpWidget.options[optionsCounter].c_str());
                    }

                    o->value(tmpWidget.value);

                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_CHOICE);
                    theStack->addFXGUIInfo(info,o);
                    hPack->add(o);
                }
                break;

                case FX_GUI_TEXTURE: {

                    Fl_Choice_gfc* o=new Fl_Choice_gfc(0,0,75,20,tmpWidget.label.c_str());
                    o->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
					o->textsize(controlTextSize);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    o->copy_label(o->label());
                    o->add("Previous");
                    o->add("Track A");
                    o->add("Track B");
                    o->add("Track C");
                    o->add("Track D");

                    o->value(tmpWidget.value);

                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_TEXTURE);
                    theStack->addFXGUIInfo(info,o);
                    hPack->add(o);
                }
                break;


                case FX_GUI_CUBE: {

                    Fl_Choice_gfc* o=new Fl_Choice_gfc(0,0,180,20,tmpWidget.label.c_str());
                    o->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
					o->textsize(controlTextSize);
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
                    o->copy_label(o->label());
                    int choiceValue=0;
                    std::vector<std::string> luts=lutManager.get3DLutNames();
                    std::vector<std::string>::iterator iter=luts.begin(), end=luts.end();

                    for ( iter; iter!=end;iter++ ) {
                        o->add(iter->c_str());

                        if (strcmp(iter->c_str(),lutManager.getLUT(tmpWidget.value).getNameNoPath().c_str())==0)
                            choiceValue=o->size()-2;
                    }


                    o->value(choiceValue);
                    //printf("choiceValue=%i\n",choiceValue);
                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_CUBE);
                    theStack->addFXGUIInfo(info,o);
                    hPack->add(o);
                }
                break;

                case FX_GUI_LUT: {

                    Fl_Choice_gfc* o=new Fl_Choice_gfc(0,0,180,20,tmpWidget.label.c_str());
                    o->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
                    o->labelsize(controlFontSize);
					o->textsize(controlTextSize);
                    o->copy_label(o->label());
                    o->labelfont(FL_HELVETICA);
                    o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));

                    int choiceValue=0;
                    std::vector<std::string> luts=lutManager.get1DLutNames();
                    std::vector<std::string>::iterator iter=luts.begin(), end=luts.end();

                    for ( iter; iter!=end;iter++ ) {
                        o->add
                        (iter->c_str());

                        if (strcmp(iter->c_str(),lutManager.getLUT(tmpWidget.value).getNameNoPath().c_str())==0)
                            choiceValue=o->size()-2;
                    }


                    o->value(choiceValue);

                    //link the gui objects to the FX objects and add the callback
                    o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
                    fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_LUT);
                    theStack->addFXGUIInfo(info,o);
                    hPack->add(o);
					Fl::check();
					o->do_callback(o,(this));
                }
                break;
                }//switch widget type
            }//for each widget
            if (hPack!=NULL) {
                //vPack->add(hPack);
            }
            vPack->init_sizes();

            widgetGroup->add(vPack);

        }
        //***********END INDIVIDUAL CONTROLS********/
    }
}

Fl_Group* FXControlWindow::createFX(gfcFX &theFX) {

    //1. This pack contains the general controls pack and controls pack.
    Fl_Pack* generalAndControlPacker = new Fl_Pack(0, 0, 10, 1);
    generalAndControlPacker->type(Fl_Pack::VERTICAL);
    generalAndControlPacker->spacing(0);
    generalAndControlPacker->box(FL_BORDER_FRAME);
	generalAndControlPacker->color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));
	generalAndControlPacker->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
    //generalAndControlPacker->color(FL_GREEN);
    std::string actualLabel;
    if (theFX.errorWhileLoading) {
        actualLabel = theFX.name + "(Load Error, don't use)";
        generalAndControlPacker->copy_label(actualLabel.c_str());
        generalAndControlPacker->labelcolor(fl_rgb_color(120,60,60));
        generalAndControlPacker->labelsize(10);
        actualLabel = "ERROR LOADING FX, PLEASE DON'T USE IT AND CHECK THE FX SOURCE CODE\n\n"+theFX.description;
    } else {
        generalAndControlPacker->copy_label(theFX.name.c_str());
        //generalAndControlPacker->labelcolor(fl_rgb_color(255,255,255));
        generalAndControlPacker->labelsize(14);
        actualLabel=theFX.description;
    }


    //char *tmpchar = new char[strlen(actualLabel.c_str())+1];
    //strcpy(tmpchar,actualLabel.c_str());
    generalAndControlPacker->tooltip(strdup(actualLabel.c_str()));
    generalAndControlPacker->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    generalAndControlPacker->labelfont(FL_HELVETICA_BOLD);

    generalAndControlPacker->end();

    //2. This dummy adds space for the FX label
    Fl_Group* Dummy = new Fl_Group(0, 0, 10, 20);
    Dummy->end();
    generalAndControlPacker->add(Dummy);

    //3. Here we create the general controls
    generalAndControlPacker->add(createGeneralControls(theFX));

    //4. Here we create the particular controls
    createParticularControls(theFX,generalAndControlPacker);

    generalAndControlPacker->init_sizes();
    return generalAndControlPacker;
}

Fl_Pack* FXControlWindow::createAppliedFXPane(int pquadrant) {
    quadrant=pquadrant;
    theStack=plateManager.getFXStack(quadrant);
    if (!theStack) {
        printf("Could not obtain FX stack for plate %i\n",quadrant);
        return new Fl_Pack(0,0,0,0);
    }

    int fxNameHeight=25;
    fxPacker = new Fl_Pack(bottomPaneScroll->x()+5,menuBar->h(), bottomPaneScroll->w()-20, 1);
    //fxPacker->resizable(fxPacker);
    fxPacker->type(Fl_Pack::VERTICAL);
    //fxPacker->box(FL_THIN_UP_FRAME);
    //fxPacker->color(FL_GREEN);
    fxPacker->spacing(5); //a little space between each fx
    fxPacker->end();

    int numOfFX=theStack->getNumOfFXs();
    for (fxCount=0;fxCount<numOfFX;fxCount++) {

        gfcFX tmpFX=theStack->getFX(fxCount);
        if (tmpFX.md5Hash!="") {
            fxPacker->add(createFX(tmpFX));
            fxPacker->init_sizes();
        }
    }

    return fxPacker;
}

/**
* Call this method only inside an fltk theWindow or group start/end, does nothing by itself.
*/
void FXControlWindow::createAvailableFXMenu() {//Creates the top panel of the FXControl Window. Fill with the loaded fx.

    //Fl_Box* o=new Fl_Box(0,0,100,100,"AVAILABLE FX PANE");
    //Fl_Box *panelTitleBox=new Fl_Box(0,0,314,15,"Loaded FX");
    {

        menuBar->add
        ("Control/FX Manager...",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_MANAGER,0);
        menuBar->add
        ("Control/LUT Manager...",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_LUT_MANAGER,0);
        menuBar->add
        ("Control/Clear All",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_CLEAR_ALL,0);
        menuBar->add
        ("Control/Save Stack",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_SAVE_STACK,0);
        menuBar->add
        ("Control/_Load Stack",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_LOAD_STACK,0);

        //add all the recent stacks.
	menuBar->add
        ("Control/_Close",0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_CLOSE,0);
	
        for (int i=sett.recentFXStacks.size()-1;i>=0;i--) {

            char tmp[300];
            {
                std::string tmpStringName=sett.recentFXStacks[i];
                //printf("tmpStringName=%s\n",tmpStringName.c_str());

                AddMenuSlash(tmpStringName);


                sprintf(tmp,"Control/%s",tmpStringName.c_str());


            }
            menuBar->add
            (tmp,0,(Fl_Callback*)fxMenuCB,(void*)FX_MENU_RECENT,0);

            if (i==0) {
                //sprintf(tmp,"Control/_",sett.recentFXStacks[i].c_str());

            }

        }
	
	//create the favorites menus
	
		
	for(int i=0;i<5 ;i++ ){
	char tmp[300];
	
	sprintf(tmp,"Favorites/%s %i",i==4?"_Load Stack":"Load Stack",i+1);
	menuBar->add(tmp,int(0x1ffbe)+i,(Fl_Callback*)fxMenuCB,(void*)(FX_MENU_LOAD_FAVORITES_0+i),0);
	
	}
	
	for(int i=0;i<5 ;i++ ){
	char tmp[300];
	
	sprintf(tmp,"Favorites/%s %i",i==4?"_Append Stack":"Append Stack",i+1);
	menuBar->add(tmp,int(0x4ffbe)+i,(Fl_Callback*)fxMenuCB,(void*)(FX_MENU_APPEND_FAVORITES_0+i),0);
	
	}
	
	for(int i=0;i<5 ;i++ ){
	char tmp[300];
	
	sprintf(tmp,"Favorites/Save stack to.../%s %i","Stack",i+1);
	menuBar->add(tmp,int(0x5ffbe)+i,(Fl_Callback*)fxMenuCB,(void*)(FX_MENU_SAVE_FAVORITES_0+i),0);
	}
	
	

        //int separatorIndex=menuBar->add("Control/_");


        

		//before adding the available ones, add the recent ones
		int recentOnes=sett.recentFXs.size();
		for (int i=recentOnes-1; i>=0;i--)
		{
			std::string tmpMenuName="Available FX/";
			if (i==0)
			{ //last one, add separator
				tmpMenuName+="_";
			}
			tmpMenuName+=sett.recentFXs[i];
			

			menuBar->add
				(tmpMenuName.c_str(),0,(Fl_Callback*)fxMenuCB,(void*)-fxManager.getFXIndexByName(sett.recentFXs[i]),0);
		}

		
        //get the available menu vector from fxManager
        std::vector<std::string> menuNames=fxManager.getMenuNames();
        int fxAmount=menuNames.size();
        {
            std::string tmpMenuName;
            for (int numFX=0;numFX<fxAmount;numFX++) {

                { //CODE TO PRINT THE TITLE BUT NOW IT IS THE USE LABEL

                    tmpMenuName="Available FX/";
                    tmpMenuName+=menuNames[numFX];

                    menuBar->add
                    (tmpMenuName.c_str(),0,(Fl_Callback*)fxMenuCB,(void*)-numFX,0);

                }



            } //for each FX
            // o->end(); //end pack
        } //scroll pack end
    }
    menuBar->redraw();

}



/**
*  Loads an XML formated FXS file and applies the effects contained in it to the currently active quadrant and sets the values stored in the file to each fx.
If an FX plugin contained in the file is not loaded, or if a LUT or CUBE is not loaded, it is simply skipped and a warning window shows up at the end.
* @param pfileName
*/
// void FXControlWindow::loadStack(const char *pfileName) {
//
// }

/**
* Creates an xml file with an fxs extension that contains the currently applied FX stack and all the fx's parameters. The FX's are stored by name, so they can be accesed later in a machine that loaded the FX's in a different order.
* @param pfileName
*/
// void FXControlWindow::saveStack(const char *pfileName) {
//
// }



// void FXControlWindow::createAppliedFXPaneOld(int pquadrant) {
// 	//Creates the botom panel of the FXControl Window, with the applied fxs for this quadrant, also set the values to the values that are already set for that fx for that quadrant.
// 	//call this method only inside an fltk theWindow or group start/end, does nothing by itself.
//
//
// 	quadrant=pquadrant;
// 	gfcFXStack* theStack=plateManager.getFXStack(quadrant);
// 	if (!theStack) {
// 		printf("Could not obtain FX stack for plate %i\n",quadrant);
// 		return;
// 	}
// 	{
// 		Fl_Pack* o = new Fl_Pack(bottomPaneScroll->x(), bottomPaneScroll->y(), bottomPaneScroll->w()-20, 30);
// 		o->resizable(o);
// 		o->spacing(5);
//
// 		int numOfFX=theStack->getNumOfFXs();
//
// 		for (int fxCount=0;fxCount<numOfFX;fxCount++) {
// 			int widgetSeparation=5;
// 			gfcFX theFX=theStack->getFX(fxCount);
// 			floatsAmount=theFX.floatsNum;
// 			texturesAmount=theFX.textureNum;
// 			cubesAmount=theFX.cubesNum;
// 			boolsAmount=theFX.boolsNum;
// 			int gFinalSize=30+25+15+8+8+8+floatsAmount*(15+widgetSeparation+widgetSeparation)+texturesAmount*(20+widgetSeparation)+cubesAmount*(20+widgetSeparation) +boolsAmount*(20+widgetSeparation);
//
// 			//printf("Creating applied fx no. %i\n",fxCount);
//
//
//
// 			Flu_Collapsable_Group* g=new Flu_Collapsable_Group(0,0,theFX.sizeX+10,theFX.sizeY+45,theFX.name.c_str());
// 			g->box(FL_DOWN_FRAME);
// 			g->color(FL_BLACK);
// 			g->collapse_time( 0.1f );
// 			g->copy_label(theFX.name.c_str());
// 			g->end();
// 			//g->open(theFX.guiOpen);
//
// 			{
//
// 				int groupOffsetY=g->y()+40;
// 				int groupOffsetX=g->x()+5;
//
//
// 				Fl_Pack *containAllVertPack=new Fl_Pack(5, groupOffsetY,10,g->w(),"");
// 				containAllVertPack->type(Fl_Pack::VERTICAL);
// 				{
// 					{ //create generic FX control widgets, On/Off, Remove, Move Up, move Down etc;, all inside a vertical pack group
// 						Fl_Pack *generalControlPack=new Fl_Pack(5, groupOffsetY,10,20,"");
// 						generalControlPack->type(Fl_Pack::HORIZONTAL);
// 						generalControlPack->spacing(10);
// 						{
//
//
//
// 							Fl_Check_Button* o = new Fl_Check_Button(0,groupOffsetY, 25, 25, "On/Off");
// 							o->copy_label("On/Off");
// 							o->labelcolor(FL_WHITE);
// 							o->labelfont(FL_HELVETICA_BOLD);
// 							o->labelsize(14);
// 							o->align(FL_ALIGN_TOP);
// 							o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 							fxParamInfo info(fxCount,quadrant,"NO USE: ACTIVATE OR NOT","NOGROUP",FX_ACTIVATE);
// 							theStack->addFXGUIInfo(info,o);
// 							o->value(theFX.active);
// 						}
// 						{
// 							//Fl_Button* o = new Fl_Button(groupOffsetX+40,groupOffsetY, 45, 15, "Delete");
// 							Fl_Button* o = new Fl_Button(0,groupOffsetY, 45, 15, "Delete");
// 							o->color(FL_BLACK);
// 							o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 							o->labelcolor(FL_RED);
// 							fxParamInfo info(fxCount,quadrant,"NO USE: DOWN","NOGROUP", FX_DELETE);
// 							theStack->addFXGUIInfo(info,o);
//
// 						}
// 						{
// 							//                            Fl_Button* o = new Fl_Button(groupOffsetX+40,groupOffsetY, 45, 15, "Reset");
// 							Fl_Button* o = new Fl_Button(0,groupOffsetY, 45, 15, "Reset");
// 							o->color(FL_BLACK);
// 							o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 							fxParamInfo info(fxCount,quadrant,"NO USE: DOWN","NOGROUP", FX_RESET);
// 							theStack->addFXGUIInfo(info,o);
//
// 						}
// 						{
// 							Fl_Button* o = new Fl_Button(0,groupOffsetY, 45, 20, "@8UpArrow");
// 							o->color(FL_BLACK);
// 							o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 							fxParamInfo info(fxCount,quadrant,"NO USE: MOVE UP","NOGROUP",FX_MOVEUP);
// 							theStack->addFXGUIInfo(info,o);
//
//
// 						}
// 						{
// 							Fl_Button* o = new Fl_Button(0,groupOffsetY+20, 45, 20, "@2UpArrow");
// 							o->color(FL_BLACK);
// 							o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 							fxParamInfo info(fxCount,quadrant,"NO USE: MOVE DOWN","NOGROUP",FX_MOVEDOWN);
// 							theStack->addFXGUIInfo(info,o);
//
// 							groupOffsetY+=o->h()+widgetSeparation;
// 						}
// 						generalControlPack->end();
// 						containAllVertPack->add(generalControlPack);
// 					}
// 					//Create all the bool widgets, add a little separation inbetween the previous group of widgets.
// 					groupOffsetY+=8;
//
//
// 					//create a pack group to pack all the groups tightly.
// 					{
// 						Fl_Pack *pack=new Fl_Pack(1,0,g->w()-2,g->h(),"");
//
// 						//iterate through the groups of widgets
// 						std::map<std::string, gfcFXWidgetGroup>::iterator groupsIter=theFX.groups.begin();
// 						std::map<std::string, gfcFXWidgetGroup>::iterator groupsIterEnd=theFX.groups.end();
// 						for (groupsIter;groupsIter!=groupsIterEnd;groupsIter++) {
//
// 							//create a flu simple group with the groups name
// 							{
// 								Flu_Simple_Group *simpleGroup=new Flu_Simple_Group(0,0,groupsIter->second.sizeX,groupsIter->second.sizeY,groupsIter->second.name.c_str());
// 								//Flu_Simple_Group *simpleGroup=new Flu_Simple_Group(0,0,1,1,groupsIter->second.name.c_str());
// 								simpleGroup->color(FL_BLACK);
// 								simpleGroup->labelcolor(FL_WHITE);
// 								simpleGroup->copy_label(groupsIter->second.name.c_str());
// 								//iterate through the widgets of this group
//
// 								{
// 									//horizontal pack to pack all the rows of widgets, with vertical packs to pack each widget in a row.
// 									Fl_Pack *hPack=new Fl_Pack(g->x()+1,35,simpleGroup->w()+1,simpleGroup->h()); //30 is the size the label takes up
// 									//hPack->box(FL_FLAT_BOX);
// 									//hPack->color(FL_RED);
// 									hPack->spacing(15);
//
// 									//to iterate through each widget, we don't iterate the widgets map, but the widgetsOrder vector to acces the widget directly inside the map, since the order is of important.
// 									std::vector<std::string>::iterator widgetIter=groupsIter->second.widgetsOrder.begin();
// 									std::vector<std::string>::iterator widgetIterEnd=groupsIter->second.widgetsOrder.end();
// 									Fl_Pack *vPack=NULL;
// 									for (widgetIter;widgetIter!=widgetIterEnd;widgetIter++) {
// 										gfcFXWidget tmpWidget=groupsIter->second.widgets[*widgetIter]; //get the actual widget from the map using the name from the orders vector
// 										switch (tmpWidget.type) {
// 										case FX_GUI_NEWLINE:
// 											if (vPack!=NULL)
// 												vPack->end();
//
// 											vPack = new Fl_Pack(hPack->x(),1,hPack->w(),20);
// 											vPack->type(Fl_Pack::HORIZONTAL);
// 											vPack->spacing(25);
// 											//vPack->box(FL_FLAT_BOX);
// 											//vPack->color(FL_BLUE);
// 											break;
//
// 										case FX_GUI_FLOAT: {
// 											Fl_Value_Input *o = new Fl_Value_Input(0,vPack->y(),60,0,tmpWidget.label.c_str());
// 											o->copy_label(o->label());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 											o->step(tmpWidget.step);
// 											o->maximum(tmpWidget.maximum);
// 											o->minimum(tmpWidget.minimum);
// 											o->value(tmpWidget.value);
//
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_FLOAT);
//
// 											theStack->addFXGUIInfo(info,o);
// 														   }
// 														   break;
//
// 										case FX_GUI_BOOL: {
// 											Fl_Check_Button *o = new Fl_Check_Button(0,vPack->y(),20,20,tmpWidget.label.c_str());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 											o->value(tmpWidget.value);
// 											o->copy_label(o->label());
//
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_BOOL);
// 											theStack->addFXGUIInfo(info,o);
// 														  }
// 														  break;
//
// 										case FX_GUI_SPACER: {
//
// 											Fl_Box *o = new Fl_Box(groupOffsetX,groupOffsetY,tmpWidget.width,1,"");
// 											o->box(FL_NO_BOX);
// 											o->align(FL_ALIGN_INSIDE);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 															}
// 															break;
//
// 										case FX_GUI_CHOICE: {
//
// 											Fl_Choice* o=new Fl_Choice(groupOffsetX,groupOffsetY,75,20,tmpWidget.label.c_str());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 											o->copy_label(o->label());
// 											for (int optionsCounter=0;optionsCounter<tmpWidget.options.size();optionsCounter++) {
// 												o->add
// 													(tmpWidget.options[optionsCounter].c_str());
// 											}
//
// 											o->value(tmpWidget.value);
//
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_CHOICE);
// 											theStack->addFXGUIInfo(info,o);
// 															}
// 															break;
//
// 										case FX_GUI_TEXTURE: {
//
// 											Fl_Choice* o=new Fl_Choice(groupOffsetX,groupOffsetY,75,20,tmpWidget.label.c_str());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 											o->copy_label(o->label());
// 											o->add
// 												("Previous");
// 											o->add
// 												("Track A");
// 											o->add
// 												("Track B");
// 											o->add
// 												("Track C");
// 											o->add
// 												("Track D");
//
// 											o->value(tmpWidget.value);
//
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_TEXTURE);
// 											theStack->addFXGUIInfo(info,o);
// 															 }
// 															 break;
//
//
// 										case FX_GUI_CUBE: {
//
// 											Fl_Choice* o=new Fl_Choice(groupOffsetX,groupOffsetY,180,20,tmpWidget.label.c_str());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
// 											o->copy_label(o->label());
// 											int choiceValue=0;
// 											std::vector<std::string> luts=lutManager.get3DLutNames();
// 											std::vector<std::string>::iterator iter=luts.begin(), end=luts.end();
//
// 											for ( iter; iter!=end;iter++ ) {
// 												o->add
// 													(iter->c_str());
//
// 												if (strcmp(iter->c_str(),lutManager.getLUT(tmpWidget.value).getNameNoPath().c_str())==0)
// 													choiceValue=o->size()-2;
// 											}
// 											/*
// 											for(int lutCount=0;lutCount<lutArray.size();lutCount++)
// 											{
// 											if(lutArray[lutCount].type==BASELIGHT3DCUBE || lutArray[lutCount].type==IMAGELUT2D)
// 											{
// 											o->add
// 											(lutArray[lutCount].getNameNoPath().c_str());
// 											if(strcmp(lutArray[lutCount].getNameNoPath().c_str(),lutArray[tmpWidget.value].getNameNoPath().c_str())==0)
// 											choiceValue=o->size()-2;
// 											}
// 											}*/
//
//
// 											o->value(choiceValue);
// 											printf("choiceValue=%i\n",choiceValue);
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_CUBE);
// 											theStack->addFXGUIInfo(info,o);
// 														  }
// 														  break;
//
// 										case FX_GUI_LUT: {
//
// 											Fl_Choice* o=new Fl_Choice(groupOffsetX,groupOffsetY,180,20,tmpWidget.label.c_str());
// 											o->align(FL_ALIGN_TOP);
// 											o->labelsize(12);
// 											o->copy_label(o->label());
// 											o->labelfont(FL_HELVETICA_BOLD);
// 											o->labelcolor(fl_rgb_color(tmpWidget.labelColor.x,tmpWidget.labelColor.y,tmpWidget.labelColor.z));
//
// 											int choiceValue=0;
// 											std::vector<std::string> luts=lutManager.get1DLutNames();
// 											std::vector<std::string>::iterator iter=luts.begin(), end=luts.end();
//
// 											for ( iter; iter!=end;iter++ ) {
// 												o->add
// 													(iter->c_str());
//
// 												if (strcmp(iter->c_str(),lutManager.getLUT(tmpWidget.value).getNameNoPath().c_str())==0)
// 													choiceValue=o->size()-2;
// 											}
// 											/*
// 											for(int lutCount=0;lutCount<lutArray.size();lutCount++)
// 											{
// 											if(lutArray[lutCount].type==JEFECHECK1D)
// 											{
// 											o->add
// 											(lutArray[lutCount].getNameNoPath().c_str());
// 											if(strcmp(lutArray[lutCount].getNameNoPath().c_str(),lutArray[tmpWidget.value].getNameNoPath().c_str())==0)
// 											choiceValue=o->size()-2;
// 											}
// 											}*/
//
// 											o->value(choiceValue);
//
// 											//link the gui objects to the FX objects and add the callback
// 											o->callback((Fl_Callback*)generalFXGUICB, (void*)(this));
// 											fxParamInfo info(fxCount,quadrant,tmpWidget.varName.c_str(),groupsIter->second.name.c_str(),FX_GUI_LUT);
// 											theStack->addFXGUIInfo(info,o);
// 														 }
// 														 break;
// 										}
//
// 									}
// 									vPack->end(); //end the LAST vPack
// 									hPack->end(); //end the hPack
// 								}
//
// 								simpleGroup->end();
// 							}
//
//
//
// 						}
// 						pack->end(); //end the pack that packs the groups
// 						containAllVertPack->add(pack);
// 					}
// 					g->add(containAllVertPack);
// 					//o->end(); //end the collapsible group
// 				}
// 			}
// 			//g->end();
// 		}
// 		o->end();
// 	}
//
// }
//



void FXControlWindow::scheduleUpdateWindow(int quadrant)
{
	updateWindowScheduled=1;
	updateWindowQuadrant=quadrant;
}
