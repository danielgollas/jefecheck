#include "gfcfxmanager.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "UIConstants.h"
#include "gfcStructures.h"
#ifdef JEFECHECK_USE_FLTK
#include <FL/Fl_File_Chooser.H>
#endif
extern gfcSettings sett;
#ifdef JEFECHECK_USE_FLTK
#include "gfcfilechooser.h"
extern NativeFileChooser *fc;
extern void save_input_file ( Fl_File_Chooser *w, void *userdata );

#include "fxWindow.h"
extern FXWindow fxw;

#include "fxcontrolwindow.h"
extern FXControlWindow fxControlWindow1;
#endif

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
#ifdef JEFECHECK_USE_FLTK
    //5. Update the FX Control Window to show the newly loaded FX in the add menu.
    fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);
#endif
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
#ifdef JEFECHECK_USE_FLTK
    //printf("Filling FX Manager window\n");


    //1. Clear the scroll group and begin it (remember to end it too)
    loadedScroll->clear();
    loadedScroll->begin();

    int counter=0;
    {
        //2. Create a new packed group inside the scroll to tightly pack the FXs
        Fl_Pack *p=new Fl_Pack ( loadedScroll->x() +5,loadedScroll->y() +5,loadedScroll->w()-20,5 );
        p->box ( FL_DOWN_FRAME );


        /*3. Iterate through all the loaded FXs and generate their entry in the GUI.
        *Group to contain the rest of the widgets
        *Name with a comprehensive tooltip
		*Reload button
        *Unload button
        *Autoload button set to the correct value (the FXs value ORed with the Autoload All value)
        The unload and autoload buttons have their calblacks set appropiately and send their index as user data
        Remember to cloase the Group
        */
        std::vector<gfcFX>::iterator iter=fxArray.begin();
        std::vector<gfcFX>::iterator end=fxArray.end();
        for ( iter;iter<end;iter++ ) {
            {
                Fl_Group *g= new Fl_Group ( 0,0,40,20,"Hello" );
                g->box ( FL_BORDER_BOX );
				g->color( fl_rgb_color(GFC_BG_COLOR));

                
				if(iter->errorWhileLoading){
					g->copy_label ( (iter->name+"(Load Error)").c_str() );
					g->labelcolor ( fl_rgb_color(65,20,20) );
				}
				else{
				g->copy_label ( iter->name.c_str() );
				g->labelcolor ( fl_rgb_color(85,85,85) );
				}
                
                g->align ( FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP );
                char *tmpTooltip=new char[4096];
				sprintf ( tmpTooltip,"Name: %s\nAuthor:%s\nVersion:%s\nDescription: %s\nFile: %s\n%s%s",iter->name.c_str(),iter->author.c_str(),iter->version.c_str(),iter->description.c_str(),iter->filename.c_str(),iter->errorWhileLoading?"CompilationError":"",iter->errorWhileLoading?iter->compilationError.c_str():"" );
                g->tooltip ( tmpTooltip );


                {
                    Fl_Button_gfc* o=new Fl_Button_gfc ( g->x() +g->w()-15,g->y() +2,5,15,"Reload" );
                    o->callback ( ( Fl_Callback* ) fxManagerCB_RELOAD, ( void* ) counter);
                }

                {
                    Fl_Check_Button* c=new Fl_Check_Button ( g->x() +g->w()-10, g->y() +2,  5,15, "Auto-Load" );
                    c->value (iter->autoload);
                    c->labelcolor ( fl_rgb_color(85,85,85) );
					c->down_box(FL_FLAT_BOX);
					c->color(fl_rgb_color(85,85,85));
					c->selection_color(fl_rgb_color(160,160,160));
                    c->callback ( ( Fl_Callback* ) fxManagerCB_AUTOLOAD, ( void* ) counter );
                }
				
				{
					Fl_Button_gfc* o=new Fl_Button_gfc ( g->x() +g->w()-2, g->y() +2, 2,15, "@1+" );
					o->labelcolor ( fl_rgb_color(85,85,85) );
					o->callback ( ( Fl_Callback* ) fxManagerCB_DELETE, ( void* ) counter );
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

#ifdef JEFECHECK_USE_FLTK
void fxManagerCB_OTHERS(Fl_Widget * o, void * v) {
    printf("Others\n");


    switch ( (long)v ) {

    case FXDONE_ID:
        fxw.fxWindow->hide();
        break;
    case FXBROWSE_ID: {
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
        fc->filter ( "JefeCheck FX(*.{jfx})" );
	
        fc->show();
        fc->directory ( sett.lutPath.c_str() );
        while ( fc->shown() )
            app().waitForEvents();

        fxManager.saveScrollPosition();
        
        for (int i=1;i<=fc->count();i++)
            fxManager.loadFX(fc->value(i));

        fxManager.restoreScrollPosition();
        
        networkManager.startFXSinc();
        
        fc->filter(prevFilter.c_str());
        fc->directory ( prevDirectory.c_str() );
        fc->type(prevType);
    }

    break;


    case FXUNLOADALL_ID:
        printf("Unload All\n");
        fxManager.deleteAllFX();
        break;


    case FXAUTOLOADALL_ID:
        printf("Autoload All %i\n",((Fl_Button*)o)->value());
        fxManager.autoLoadAll(((Fl_Button*)o)->value());
        break;
    }
}

void fxManagerCB_RELOAD(Fl_Widget * o, void * v) {
	printf("Reload FX %i\n",(long)v);
	std::string filename=fxManager.getFX(long(v)).filename;
	fxManager.deleteFX((long)v);
	fxManager.loadFX(filename);
}

void fxManagerCB_DELETE(Fl_Widget * o, void * v) {
    printf("Delete FX %i\n",(long)v);
    fxManager.deleteFX((long)v);
}

void fxManagerCB_AUTOLOAD(Fl_Widget * o, void * v) {
    printf("Autoload FX %i\n",(long)v);
    fxManager.setAutoLoad((long)v,((Fl_Button*)o)->value());

}
#endif  // JEFECHECK_USE_FLTK

void gfcFXManager::autoLoadAll(bool autoload) {
    printf("Autoloading All!\n");

    for (int i=fxArray.size()-1;i>=0;i--)
        setAutoLoad(i,autoload);

    fxManager.saveScrollPosition();
    fillLoadedScroll();
    fxManager.restoreScrollPosition();
}

void gfcFXManager::initWidgets() {
#ifdef JEFECHECK_USE_FLTK
    progress=fxw.progress;
    loadedScroll=fxw.scrollLoaded;
    autoloadAllButton=fxw.autoLoadAllButton;
#endif
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


