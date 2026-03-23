// CLI11 must be included before any X11/OIIO headers to avoid macro conflicts
#include "CLI11.hpp"

// ***************************************************************
//  main   version:  1.0     date: 03/20/2006
//  -------------------------------------------------------------
/*  /********************************************************************
	created:	2006/03/20
	created:	20:3:2006   12:35
	filename: 	E:\projects\gfcheck\gfcheck\main.cpp
	file path:	E:\projects\gfcheck\gfcheck
	file base:	main
	file ext:	cpp
	author:		Daniel Gollas�

	purpose:	Main procedure for gFcheck frame Cycler
*********************************************************************/
//  -------------------------------------------------------------
//  Copyright (C) 2006 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************

#pragma warning( once : 4275)


#include <glad/glad.h>
#include <stdio.h>
#include <FL/Fl.H>
#include "GlViewport.h"
#include "dpxslice.h"
#include "lut1d.h"
#include <stdio.h>
#include "mainWindow.h"
#include "loadWindow.h"
#include "lutWindow.h"
#include "fxWindow.h"
#include "fxcontrolwindow.h"
#include "playlistwindow.h"
#include "preferencesWindow.h"
#include "gfcStructures.h"
#include <stdlib.h>
#include <map>
#include <string>
#include "gfcSequence.h"
#include "aboutWindow.h"
#include "renderWindow.h"
#include "remoteWindow.h"
#include "drawingToolsWindow.h"
#include "gfcTextRenderer.h"
#include <FL/Fl_Text_Buffer.H>

#ifdef WIN32
//stuff needed to set the program icon on windows
#include <FL/x.H>
#include "resource1.h"
#endif

#include "minSpecsWindow.h"
MinSpecsWindow reqW(0,0,300,300,"");


GLuint gWatermarkTextureID=0;





#include "gfcnotetext.h"


Fl_Text_Buffer remoteLogBuffer;

#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <math.h>
#include "gfcfx.h"
//#include <GetTime.h>
#include <iostream>
#include <fstream>

#include "RakPeerInterface.h"

#ifdef WIN32
#include <windows.h>
#endif




#include "UICallbacks.h"
//#include "network.h"


MainWindow mw(0,0,300,300,"It's Broken Cycler - Main Window");
LoadWindow lw(300,300,300,300,"Load Window");
PreferencesWindow pw;
LutWindow lutw(300,300,300,300,"LUT Window");
FXWindow fxw(300,300,300,300,"FX Window");
FXControlWindow fxControlWindow1;
PlaylistWindow plw;
RenderWindow rw;
RemoteWindow rmw;
DrawingToolsWindow dtw(0,0,300,300,"Window");

extern std::vector<int> fxArrayActiveCount;


AboutWindow aboutWindow(300,300,300,300,"about");
LUT1D testLut;
#include "gfcfilechooser.h"
NativeFileChooser *fc;
bool mainWindowExists=false;
bool npotTextures=false;
extern int globalCB(int);
//extern std::vector<CubeLUT> lutArray;
bool quitNow=false;
gfcSettings sett;
void* gGLContext=NULL;
bool glReady=false;

extern int fullscreenActive;
extern int fsX,fsY,fsW,fsH;


#include "gfctrackmanager.h"
gfcTrackManager trackManager;

#include "gfcplatemanager.h"
gfcPlateManager plateManager;

#include "gfcpickmanager.h"
gfcPickManager pickManager;

#include "gfcplaylistmanager.h"
gfcPlaylistManager playlistManager;

#include "gfcplaybackmanager.h"
gfcPlaybackManager playbackManager;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcmemorymanager.h"
extern gfcMemoryManager memoryManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcnetworklog.h"
extern gfcNetworkLog networkLog;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;



enum argEnum {
    TRACKA_FILENAME,
    TRACKB_FILENAME,
    TRACKC_FILENAME,
    TRACKD_FILENAME,
    TARGET_FPS,
    LOOP_MODE,
    FRAME_MODE,
    ACTIVE_CHANELS
};


GLboolean CheckExtension( char *extName ) {
    /*
     ** Search for extName in the extensions string.  Use of strstr()
     ** is not sufficient because extension names can be prefixes of
     ** other extension names.  Could use strtok() but the constant
     ** string returned by glGetString can be in read-only memory.
     */
    char *p = (char *) glGetString(GL_EXTENSIONS);
    char *end;
    int extNameLen;

    extNameLen = strlen(extName);
    end = p + strlen(p);

    while (p < end) {
        int n = strcspn(p, " ");
        if ((extNameLen == n) && (strncmp(extName, p, n) == 0)) {
            return GL_TRUE;
        }
        p += (n + 1);
    }
    return GL_FALSE;
}


void initOpenGL() {
    glClearColor(0.2,0.2,0.2,0.2);
    glColor3f(0,0,1);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    //glEnable(GL_CULL_FACE);
    mw.vp->mode(FL_DOUBLE | FL_ALPHA | FL_STENCIL);
    glDisable(GL_DEPTH_TEST);
    glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);


}



template<class T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
    copy(v.begin(), v.end(), std::ostream_iterator<T>(std::cout, " "));
    return os;
}

void parseArguments(int argc, char *argv[]) {

    using namespace std;

    for (int i=0;i<argc; i++) {
        printf("Arg%i: %s\n",i,argv[i]);
    }

    CLI::App app{"JefeCheck - Professional Video Frame Player"};

    vector<int> fromFrames, toFrames, scaleValues;
    int frameRate = 0;
    vector<string> fxFiles, lutFiles, inputFiles;

    app.add_option("-f,--from", fromFrames, "Start loading from this frame");
    app.add_option("-t,--to", toFrames, "Stop loading at this frame");
    app.add_option("-s,--scale", scaleValues, "Scale percentages for sequences");
    app.add_option("-r,--frameRate", frameRate, "Playback frame rate");
    app.add_option("-x,--fx", fxFiles, "FX Stack files");
    app.add_option("-l,--lut", lutFiles, "LUT files for plates");
    app.add_option("input", inputFiles, "Input files")->expected(-1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        app.exit(e);
        return;
    }

    if (!inputFiles.empty()) {
        std::cout << "Got input files!\n";

        if (lowerCase(GetExtension(inputFiles.front()))=="jcs") {
            sessionManager.loadSession(inputFiles.back());
            inputFiles.erase(inputFiles.begin());
        }

        if (!inputFiles.empty() && lowerCase(GetExtension(inputFiles.front()))=="jpl") {
            playlistManager.loadPlaylist(inputFiles.back());
            inputFiles.erase(inputFiles.begin());
        }

        for (int i=0; i<GFC_MAX_SEQUENCES && i<(int)inputFiles.size(); i++) {
            gfcLoadParams params;

            if (i<(int)scaleValues.size())
                params.scale=scaleValues[i];

            if (i<(int)fromFrames.size())
                params.fromFrame=fromFrames[i];

            if (i<(int)toFrames.size())
                params.toFrame=toFrames[i];

            params.fileName=inputFiles[i];
            trackManager.loadFromFilename(i,params);
        }

        playbackManager.setInPoint(0);
        playbackManager.setOutPoint(trackManager.getMaxTrackLength());
    }

    if (frameRate > 0) {
        playbackManager.setTargetFPS(frameRate);
    }

    if (!fxFiles.empty()) {
        printf("fxCount %i\n",(int)fxFiles.size());
        for (int i=0; i<(int)fxFiles.size(); i++) {
            plateManager.loadStackFromFile(i,fxFiles[i]);
        }
    }

    if (!lutFiles.empty()) {
        printf("lutCount %i\n",(int)lutFiles.size());
        for (int i=0; i<(int)lutFiles.size(); i++) {
            plateManager.setLUTByName(i,lutFiles[i]);
        }
    }

}


/**
 *
 * @param argc
 * @param argv[]
 * @return
 */
int main(int argc, char *argv[]) {
    setbuf(stdout, NULL); // disable buffering for debug output

    printf("--------------------\nJefeCheck %s \nDaniel Gollas Gilman\n--------------------\n",JEFE_VERSION);

#ifdef linux
    printf("Running on Linux\n");
#endif

#ifdef WIN32
    printf("Running on Win32\n");
#endif

#ifdef __APPLE__
    printf("Running on Mac\n");
#endif

    setMacExecutablePath(argv[0]);
    printf("Application Data Path: %s\n",getApplicationDataPath().c_str());


	//enable fltk multithreading
	//Fl::lock();
	//Fl::unlock();
  
    mw.make_window();
    pw.make_window();
    rw.make_window();

    rmw.make_window();
    rmw.log->buffer(remoteLogBuffer);
    aboutWindow.make_window();

    reqW.make_window();

    mw.vp->ID=MAINVP_ID;

#ifndef linux
    rw.createMovie->deactivate();
#endif

#ifdef USEFREEIMAGE
#if defined(FREEIMAGE_LIB) || !defined(WIN32)
	printf("Initializing FreeImage...");
	FreeImage_Initialise();
	printf("ok\n");
#endif
#endif 
    printf("Initializing GLUT...");
    glutInit(&argc,argv);
    printf("ok\n");
    gfcTimer testTimer ( "testTimer" );    
    testTimer.start();
	testTimer.print();
    fc= new NativeFileChooser(".",NULL,0,"Choose a file");

    sett.numOfPartitions=1;

    //Fl::scheme("gtk+");

    int mwx,mwy,mww,mwh;
    Fl::screen_xywh(mwx,mwy,mww,mwh,0);


    
#ifdef WIN32
    mw.mainWindow->icon((char *)LoadIcon(fl_display, MAKEINTRESOURCE(IDI_ICON0)));
#endif
    //mw.mainWindow->show(1, &argv[0]);
    mw.mainWindow->show();

    Fl::check();
    fxControlWindow1.createWindow(0);
    plw.createWindow();
    aboutWindow.aboutWindow->position(mw.mainWindow->x()+mw.mainWindow->w()/2-aboutWindow.aboutWindow->w()/2,mw.mainWindow->y()+200);
    aboutWindow.aboutWindow->border(0);
    aboutWindow.textBrowser->hide();
    aboutWindow.aboutWindow->set_modal();
    char versionString[40];
    sprintf(versionString,"v.%s",JEFE_VERSION);
    printf("versionString: %s\n",versionString);
    aboutWindow.versionLabel->copy_label(versionString);
    aboutWindow.versionLabel->redraw();
    aboutWindow.aboutWindow->show();

    //aboutWindow.aboutWindow->position(mw.mainWindow->x()+mw.mainWindow->w()/2-aboutWindow.aboutWindow->w()/2,mw.mainWindow->y()+200);

    Fl::add_handler(globalCB);

    mainWindowExists=true;

    //mw.vp->show();
    Fl::dnd_text_ops(true);

    mw.mainWindow->redraw();
    //mw.vp->draw();

    //wait until we have a good rendering context
    // Force GLAD initialization - need GL context first
    mw.vp->make_current();
    if (!gladLoadGL()) {
        printf("Warning: GLAD initialization failed, retrying after draw...\n");
    }

    while ((glGetString(GL_VERSION))==0) {
        printf("%i\n",glGetString(GL_VERSION));
        mw.mainWindow->redraw();
        mw.vp->redraw();
        Fl::check();
    }


    /******************************************************************/
    printf("\n--------------------------------------------\n");
    printf("Relevant FLTK Library Information:\n");
    printf("--------------------------------------------\n");
    printf(" *Version: %f\n", Fl::version());

    printf("\n--------------------------------------------\n");
    printf("Relevant OpenGL Implementation Information:\n");
    printf("--------------------------------------------\n");
    char tmpReqString[120];
    sprintf(tmpReqString,"GL Version: %s", (char*)glGetString (GL_VERSION));
    printf(" *%s\n", tmpReqString);
    reqW.glVersion->copy_label(tmpReqString);

    sprintf(tmpReqString,"GL Vendor: %s", (char*)glGetString (GL_VENDOR));
    printf(" *%s\n", tmpReqString);
    reqW.glVendor->copy_label(tmpReqString);

    sprintf(tmpReqString,"GL Renderer: %s", (char*)glGetString (GL_RENDERER));
    printf(" *%s\n", tmpReqString);
    reqW.glRenderer->copy_label(tmpReqString);

    int maxDims[2];
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,(GLint*)maxDims);
    sprintf(tmpReqString,"Maximum Texture Size: %ix%i",maxDims[0],maxDims[0]);
    printf(" *%s\n", tmpReqString);
    reqW.maxTexSize->copy_label(tmpReqString);

    glGetIntegerv(GL_MAX_VIEWPORT_DIMS,(GLint*)maxDims);
    sprintf(tmpReqString,"Maximum Viewport Size: x:%i, y:%i",maxDims[0],maxDims[1]);
    printf(" *%s\n", tmpReqString);
    reqW.maxViewportSize->copy_label(tmpReqString);
	
    // GLAD is initialized on first GlViewport::draw() call
    printf(" *GLAD will initialize on first render\n");

    // With GLAD/OpenGL 3.3 Core, check extension support via glGetString
    if (CheckExtension("GL_ARB_shader_objects")) {
        printf(" *Shader Objects available\n");
        sett.glsl=true;
        reqW.shaderObjects->value(true);
        reqW.shaderObjects->labelcolor(FL_GREEN);
    } else {
        printf(" *Shader Objects NOT available\n");
        sett.glsl=false;
    }

    if (CheckExtension("GL_ARB_pixel_buffer_object")) {
        printf(" *PBO available\n");
        reqW.PBO->value(true);
        reqW.PBO->labelcolor(FL_GREEN);
    } else {
        printf(" *PBO NOT available\n");
    }

    if (CheckExtension("GL_ARB_texture_float")) {
        printf(" *GL_ARB_texture_float available\n");
        sett.fp16=1;
        reqW.textureFloat->value(true);
        reqW.textureFloat->labelcolor(FL_GREEN);
    } else {
        printf(" *GL_ARB_texture_float NOT Available\n");
        sett.fp16=0;
    }

    if (CheckExtension("GL_ARB_half_float_pixel")) {
        printf(" *Half availabe\n");
        reqW.textureHalf->value(true);
        reqW.textureHalf->labelcolor(FL_GREEN);
    } else {
        printf(" *Half NOT availabe!\n");
    }



    if (CheckExtension("GL_ARB_texture_rectangle")) {
        printf(" *GL_ARB_texture_rectangle available\n");
        npotTextures=false;
        sett.textureRectangles=npotTextures?false:true;
        reqW.textureRectangle->value(true);
        reqW.textureRectangle->labelcolor(FL_GREEN);
    } else {
        printf(" *GL_ARB_texture_rectangle NOT available\n");
        npotTextures=false;
        sett.textureRectangles=false;
        //pop up a message box here saying that jefecheck cannot run without this extension.
    }
    if ( CheckExtension("GL_EXT_texture_compression_s3tc") && CheckExtension("GL_ARB_texture_compression")) {
        printf(" *Texture compression available (S3TC)\n");
        sett.textureCompression=true;
        reqW.s3tc->value(true);
        reqW.s3tc->labelcolor(FL_GREEN);
    } else {
        printf(" *Texture compression NOT available\n");
        sett.textureCompression=false;
    }

    if ( CheckExtension("GL_ARB_fragment_shader") && CheckExtension("GL_ARB_vertex_shader")) {
        printf(" *GLSL Shading Available\n");
        sett.glsl=true;
        reqW.glsl->value(true);
        reqW.glsl->labelcolor(FL_GREEN);
    } else {
        printf(" *GLSL Shading NOT available\n");
        sett.glsl=false;
    }



    if ( CheckExtension("GL_EXT_framebuffer_object")) {
        printf(" *FBOs: YES\n");
        sett.fbo=true;
        reqW.fbo->value(true);
        reqW.fbo->labelcolor(FL_GREEN);

    } else {
        printf(" *FBOs: NO\n");
        sett.fbo=false;
    }

    //TEST
    //sett.textureRectangles=false;


    printf("--------------------------------------------\n");
    Fl::check();

    //npotTextures=false; //JUST TO TEST NPOT PERFORMANCE, COMMENT AFTER DEBBUGING
    Fl::check();
    gGLContext=mw.vp->context();
    glReady=true;
    printf("Initializing OpenGL\n");
    initOpenGL();
    Fl::check();

    // Initialize text renderer
    {
        std::string fontPath = getApplicationDataPath() + "fonts/Roboto-Regular.ttf";
        std::string boldFontPath = getApplicationDataPath() + "fonts/Roboto-Bold.ttf";
        if (!textRenderer().loadFont(fontPath))
            textRenderer().loadFont("common/fonts/Roboto-Regular.ttf");
        if (!textRenderer().loadBoldFont(boldFontPath))
            textRenderer().loadBoldFont("common/fonts/Roboto-Bold.ttf");
        float dpi = mw.vp->pixels_per_unit();
        textRenderer().setDPIScale(dpi);
        textRenderer().setShadowEnabled(true);
        textRenderer().setShadowOffset(dpi, -dpi);        // 1 logical pixel down-right
        textRenderer().setShadowColor(0, 0, 0, 0.5f);
        textRenderer().setShadowBlur(0);                 // no blur — single clean shadow pass
    }
	
	GLuint testTextures[5];
	glGenTextures(5,testTextures);

    if (sett.glsl) {
        printf("Initializing Shader Objects\n");
        GLhandleARB testProgramObject=glCreateProgramObjectARB();
        Fl::check();
        //printf("glCreateProgramObjectARB checked\n");
        int waitingForTestProgramCounter=0;
        int testProgramCounterLimit=50;
        //printf(" Test Program Object=%i\n",testProgramObject);
        while ((intptr_t)testProgramObject==0 && waitingForTestProgramCounter<=testProgramCounterLimit) {
            //printf("counter=%i \n",waitingForTestProgramCounter);
            mw.mainWindow->redraw();
            //printf("main window redrawn \n");
            mw.vp->redraw();
            //printf("vp redrawn \n");
            Fl::check();
            //printf("checked \n");
            testProgramObject=glCreateProgramObjectARB();
            //printf(" Test Program Object=%i\n",testProgramObject);
            waitingForTestProgramCounter++;
        }//*/

        if (testProgramCounterLimit<=waitingForTestProgramCounter) {
            fl_alert("Could not create ProgramObjectARB, FX plugins will not work!\n");
        } else {
            printf("Shader Objects Ready (%i warmup runs)\n",waitingForTestProgramCounter);
        }
    }

    //INITIALIZE LOAD WINDOW GUI

    printf("Initilizing GUI variables\n");

    trackManager.initializeWidgets();
    plateManager.initializeWidgets();
    playbackManager.initializeWidgets(mw);
    fxManager.initWidgets();
    lutManager.initWidgets();
    networkManager.initializeWidgets();
    networkLog.initialize();

    mw.vp->invalidate(); //make sure the VP has correct transformations initially.

    // Populate font dropdown with system TrueType fonts
    {
        auto fonts = enumerateSystemFonts();
        int selectedIdx = 0;
        for (size_t i = 0; i < fonts.size(); i++) {
            // Store the font path as user_data (must be persistent — use strdup)
            pw.textDisplayFont->add(fonts[i].first.c_str(), 0, nullptr, strdup(fonts[i].second.c_str()));
            if (fonts[i].first == "Roboto" ||
                (fonts[i].first.find("Roboto") != std::string::npos &&
                 fonts[i].first.find("Bold") == std::string::npos &&
                 fonts[i].first.find("Italic") == std::string::npos))
                selectedIdx = (int)i;
        }
        pw.textDisplayFont->value(selectedIdx);
        printf("GfcTextRenderer: enumerated %zu system fonts\n", fonts.size());
    }

    readSettings(sett);

    if (sett.startFullscreen) {
		mw.toggleFullscreen();
    }

    trackManager.updateTrackWidgetsFromAndTo(playbackManager.getFromFrame(),playbackManager.getToFrame());
    playbackManager.setTargetFPS();
    fxControlWindow1.scheduleUpdateWindow(0);


    //register drawable and notifiable objects with the pick manager
    pickManager.registerDrawee(&plateManager);
    pickManager.registerNotifee(&plateManager);
    plateManager.registerPlatesAsPickNotifees();

    //TESTS**************
    //dtw.drawingToolsWindow->position(mw.vp->x(),mw.vp->y());
    /*dtw.drawingToolsWindow->show();
    dtw.notesTree->add("This is a note");
    dtw.notesTree->add("This is another note");
    dtw.reviewsTree->add("Review by Juan");*/
    //printf("size of gfcFrame: %i\n", sizeof(gfcFrame));
    //printf("size of dpxSlice: %i\n", sizeof(DpxSlice));

    //END OF TESTS********

    lw.loadWindow->position(mw.mainWindow->x()/2+lw.loadWindow->w()/2.0,mw.mainWindow->y()/2+lw.loadWindow->h()/2.0);
    if (sett.openLoadWindowAtStartup) {

        lw.loadWindow->show();
    }




    Fl::focus(mw.vp);

    if (!memoryManager.withinLimits()) {
        printf("Outside of allowed memory limits\n");
        fl_alert("It seems that you have less free RAM than you have assign JefeCheck to use.\nYou can set how much RAM you want JefeCheck to use in\nthe Preferences Dialog (File>Preferences>General>Percentage of Ram to Use)\n \nJefeCheck might not be able to load any frames until you correct that.");

        pw.preferencesWindow->show();
    }

    aboutWindow.aboutWindow->set_non_modal();
    aboutWindow.aboutWindow->hide();

    // Now that the splash is closed, set global colors for dark-themed dialogs
    // (fl_alert, fl_choice, fl_message all use these)
    Fl::set_color(FL_BACKGROUND_COLOR, 38, 38, 38);
    Fl::set_color(FL_BACKGROUND2_COLOR, 48, 48, 48);
    Fl::set_color(FL_FOREGROUND_COLOR, 200, 200, 200);
    Fl::foreground(200, 200, 200);
    Fl::background(38, 38, 38);
    Fl::background2(48, 48, 48);

    rw.path->value(sett.defaultBrowsePath.c_str());
    //parse the command line arguments if not recovering

#ifndef __APPLE__
    parseArguments(argc, argv);
#else

    //on mac, the -psnXXXXX stuff needs to be skipped
    printf("parseArguments: argc=%d\n", argc);
    for (int i=0; i<argc; i++) printf("  argv[%d]=%s\n", i, argv[i]);
    if (argc>=1) {
        if (argc>=2 && strstr(argv[1],"-psn")!=NULL) { //we have that psn shit
            //printf("Fuck mac and the PSN\n");
            parseArguments(argc-1, &argv[1]);
        } else { //parse normally
            //printf("Fuck mac, but no PSN\n");
            parseArguments(argc, argv);
        }
    }

#endif

    plateManager.updateAllFromGUI(); //update all here because if we load from command line, the plates won't know that we closed the loadWindow.
    plw.theWindow->show();
    plw.theWindow->hide();
    if (sessionManager.checkCrashedSession() && sett.enableCrashRecoverySession) {
        int answer=fl_choice("It appears JefeCheck crashed last time you used it (sorry!)\n\n    Do you want to try to recover your last session?",
                             "No, thanks", "Yes, please", "No, and don't ask me this again, ever!");

        switch ( answer ) {
        case 0:
            sessionManager.removeCrashSession();
            break;
        case 1:
            sessionManager.loadCrashedSession();
            break;

        case 2:
            sessionManager.removeCrashSession();
            sett.enableCrashRecoverySession=false;
            pw.attemptToRecoverFromCrashCheckBox->value(false);
            break;
        }
    }



    //disable navigation for the glViewport
    //mw.vp->clear_visible_focus();
    Fl::visible_focus(0);
    

    
    printf("READY>\n");
    //sett.bgColor=(sett/255.0);
    printf("bgColor=%f\n",sett.bgColor);
    int bgColorOffset=-5;
    int bgColor=sett.bgColor*255;
    if(abs(bgColor)<=abs(bgColorOffset))
    {
	    bgColorOffset*=-1;
    }

    mw.bgBox->color(fl_rgb_color(bgColor+bgColorOffset,bgColor+bgColorOffset,bgColor+bgColorOffset));
    mw.mainWindow->redraw();
    testTimer.stop();
    testTimer.print();
    
    while (!quitNow && mw.mainWindow->shown()) {
        {
            //We always update the managers no matter what we do.
            Fl::check();
            {
                if (plateManager.getChanged() ) {

                    mw.vp->redraw();
                } else {

					
					
					if(!playbackManager.isPlaying())
					{

					//HERE WE ALWAYS SLEEP A GOOD AMOUNT, TRY TO GO TO 0% CPU

#ifdef WIN32
										
                    Fl::wait(0.001);
#endif

#ifdef __APPLE__
                    //on mac we have to sleep a whole lot apparently
                    Fl::wait(0.0001);
#endif

#ifdef linux
                    Fl::wait(0.0001);
#endif
					}
					else
					{
					//here we sleep depending on the setting for trying hard. If we sleep to much, we get irregular FPS
						if (!sett.processorPriority)
						{
						
						
#ifdef WIN32

						Fl::wait(0.0005);
#endif

#ifdef __APPLE__
						//on mac we have to sleep a whole lot apparently
						Fl::wait(0.0001);
#endif

#ifdef linux
						Fl::wait(0.0001);
#endif
						}
					}
					
                }
            }
        }

        if (!lw.loadWindow->visible()) {
            playbackManager.update();
			
        }
		else{
		//apple needs it's sleep, even if we are not playing back.
		#ifdef __APPLE__
            //on mac we have to sleep a whole lot apparently
          Fl::wait(0.01);
		#endif

		}

        networkManager.update();
        trackManager.generateTextures();
        trackManager.updateTrackWidgets();
        plateManager.updateAnimations();
        plw.updateWindow();
        fxControlWindow1.updateWindow();
	//Fl::wait();
    }

    // licenseClient.Disconnect(30);
    //sleep(1);
    printf("Running exit routine...\n");
    exitRoutine();

    printf("\nExiting JefeCheck\n\n--------------\nbye\n\n");
    //getchar();
    return 0;

    //return (Fl::run());
}
