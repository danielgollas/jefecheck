#include "gfclutmanager.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "UIConstants.h"
#include "gfcStructures.h"
#ifdef JEFECHECK_USE_FLTK
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Pack.H>
#endif

extern gfcSettings sett;
#ifdef JEFECHECK_USE_FLTK
#include "gfcfilechooser.h"
extern NativeFileChooser *fc;
extern void save_input_file ( Fl_File_Chooser *w, void *userdata );

#include "lutWindow.h"
extern LutWindow lutw;
#endif


#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

gfcLUTManager lutManager;

gfcLUTManager::gfcLUTManager()
    : loadedScroll(nullptr),
      autoloadAllButton(nullptr),
#ifdef JEFECHECK_USE_FLTK
      progress(nullptr),
#else
      // The Qt build's Fl_Progress is a no-op stub class. Allocating a
      // single owned instance keeps loadLUT and the trilerp loaders
      // (which dereference `progress` on every status tick) safe to
      // call, without auditing every `progress->...` call site.
      progress(new Fl_Progress),
#endif
      scrollPosY(0),
      scrollPosX(0) {
}


gfcLUTManager::~gfcLUTManager() {
#ifndef JEFECHECK_USE_FLTK
    // Owned in the Qt build; FLTK ones are owned by their parent group.
    delete progress;
#endif
}

#ifdef JEFECHECK_USE_FLTK
void lutManagerCB_OTHERS(Fl_Widget * o, void * v) {

	switch ( (long)v ) {
	
	case LUTDEFAULTLUT_ID:
		sett.defaultLUTName=lutManager.getLUT(lutw.defaultLUT->value()-1).name; //-1 to account for No LUT option
		sett.defaultLUTNameBackup=sett.defaultLUTName; //make this the save since we always save the backup.
		break;

	case LUTDONE_ID:
		lutw.preview->value(false);
        plateManager.setDrawLUTPreview(false,lutw.uniform->value(),lutw.loadedLuts->value());
        lutw.lutWindow->hide();
        break;
        
    case LUTCHANGEPREVIEW_ID:
    case LUTPREVIEW_ID:
    	plateManager.setDrawLUTPreview(lutw.preview->value(),lutw.uniform->value(),lutw.loadedLuts->value());
    break;
    
    case LUTBROWSE_ID: {
        printf("Browse\n");

        //printf ( " *Path: %s\n",sett.lutPath.c_str());
        char oldPath[FL_PATH_MAX];
        strcpy ( oldPath,fc->directory());
        fc->callback ( NULL );
        fc->preview ( 0 );
        std::string prevDirectory=fc->directory();
        
        std::string prevFilter=fc->filter();
        int prevType=fc->type();
        fc->type(Fl_File_Chooser::MULTI);
        fc->filter ( "Supported LUT Files(*.{lut,tga,bmp,png,cube,cub})" );

        fc->show();
        fc->directory ( sett.lutPath.c_str() );
        while ( fc->shown() )
            app().waitForEvents();

        lutManager.saveScrollPosition();

        for (int i=1;i<=fc->count();i++)
            lutManager.loadLUT(fc->value(i));

        networkManager.startLUTSinc(); //when we load a new LUTs, the networkManager starts a sinc process if we are connected.

        lutManager.restoreScrollPosition();
        fc->filter(prevFilter.c_str());
        fc->directory ( prevDirectory.c_str() );
        fc->type(prevType);
    }

    break;


    case LUTUNLOADALL_ID:
        printf("Unload All\n");
        lutManager.deleteAllLUTs();
        break;


    case LUTAUTOLOADALL_ID:
        printf("Autoload All %i\n",((Fl_Button*)o)->value());
        lutManager.autoLoadAll(((Fl_Button*)o)->value());
        break;

	default:
		lutw.preview->value(false);
		plateManager.setDrawLUTPreview(false,lutw.uniform->value(),lutw.loadedLuts->value());
		lutw.lutWindow->hide();
		break;
    }
}
#endif

#ifdef JEFECHECK_USE_FLTK
void lutManagerCB_DELETE(Fl_Widget * o, void * v) {
    printf("Delete LUT %i\n",(long)v);
    lutManager.deleteLUT((long)v);
}
#endif

#ifdef JEFECHECK_USE_FLTK
void lutManagerCB_AUTOLOAD(Fl_Widget * o, void * v) {
    lutManager.setAutoLoad((long)v,((Fl_Button*)o)->value());
}
#endif

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

#ifdef JEFECHECK_USE_FLTK
    lutw.loadedLuts->clear();
	lutw.defaultLUT->clear();
	lutw.defaultLUT->add("No LUT");
#endif

    lutHashMap.erase ( lutHashMap.begin(),lutHashMap.end() );
    for ( iter;iter!=end ;iter++ ) {
        //printf ( "Adding %i as (%s):%s\n",counter,iter->name, iter->md5Hash.c_str() );
        lutHashMap[iter->md5Hash]=counter;

#ifdef JEFECHECK_USE_FLTK
        //printf ( "LUT ARRAY SIZE=%i\n",lutArray.size() );
        lutw.loadedLuts->add
        ( GetFilenameNoPath ( iter->filename ).c_str() );

		lutw.defaultLUT->add
			( GetFilenameNoPath ( iter->filename ).c_str() );

        lutw.loadedLuts->redraw();
#endif

        counter++;


    }

#ifdef JEFECHECK_USE_FLTK
	//after that, set the default LUT to what it is
	lutw.defaultLUT->value(lutManager.getLutIndexByName(sett.defaultLUTName)+1); //+1 to account for the No LUT option
#endif
}

void gfcLUTManager::fillLoadedScroll() {
    if (!loadedScroll) {
        printf("NO GUI ASSIGNED TO LUT MANAGER!\n");
        return;
    }
#ifdef JEFECHECK_USE_FLTK
    //printf("Filling FX Manager window\n");


    //1. Clear the scroll group and begin it (remember to end it too)
    loadedScroll->clear();
    loadedScroll->begin();

    int counter=0;
    {
        //2. Create a new packed group inside the scroll to tightly pack the LUTs
        Fl_Pack *p=new Fl_Pack ( loadedScroll->x() +5,loadedScroll->y() +5,loadedScroll->w()-25,5 );
        p->box ( FL_DOWN_FRAME );


        /*3. Iterate through all the loaded LUTs and generate their entry in the GUI.
        *Group to contain the rest of the widgets
        *Name with a comprehensive tooltip
        *Unload button
        *Autoload button set to the correct value (the FXs value ORed with the Autoload All value)
        The unload and autoload buttons have their calblacks set appropiately and send their index as user data
        Remember to cloase the Group
        */
        std::vector<CubeLUT>::iterator iter=lutArray.begin();
        std::vector<CubeLUT>::iterator end=lutArray.end();
        for ( iter;iter<end;iter++ ) {
            {
                Fl_Group *g= new Fl_Group ( 0,0,40,20,"Hello" );
				g->box ( FL_BORDER_BOX );
				g->color( fl_rgb_color(GFC_BG_COLOR));

                g->copy_label ( iter->getNameNoPath().c_str() );
				
				g->labelcolor ( fl_rgb_color(85,85,85) );
                g->align ( FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP );
                char *tmpTooltip=new char[4096];
                sprintf ( tmpTooltip,"Name: %s\nType:%i",iter->getNameNoPath().c_str(),iter->type );
                g->tooltip ( tmpTooltip );


                {
                    Fl_Button_gfc* o=new Fl_Button_gfc ( g->x() +g->w()-15,g->y() +2,5,15,"Unload" );
                    o->callback ( ( Fl_Callback* ) lutManagerCB_DELETE, ( void* ) counter);
                }

                {
                    Fl_Check_Button* c=new Fl_Check_Button ( g->x() +g->w()-10, g->y() +2,  2,15, "Auto-Load" );
                    c->value (iter->autoload);
                    c->labelcolor ( fl_rgb_color(85,85,85) );
					c->down_box(FL_FLAT_BOX);
					c->color(fl_rgb_color(85,85,85));
					c->selection_color(fl_rgb_color(160,160,160));
                    c->callback ( ( Fl_Callback* ) lutManagerCB_AUTOLOAD, ( void* ) counter );
                }
                g->end();
            }
            counter++;
        }
        //4. Close pack
        p->end();

        //4. Close Scroll
        loadedScroll->end();
        loadedScroll->redraw();


    }
#ifndef __APPLE__
    app().processEvents();
#endif
#endif  // JEFECHECK_USE_FLTK
}

void gfcLUTManager::initWidgets() {
#ifdef JEFECHECK_USE_FLTK
    progress=lutw.progress;
    loadedScroll=lutw.scrollLoaded;
    autoloadAllButton=lutw.autoLoadAllButton;
#endif
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
