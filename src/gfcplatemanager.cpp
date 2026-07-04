#include "gfcplatemanager.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "gfcTextRenderer.h"
#include "qt/gfcplategui_qt.h"
#include "qt/gfcplatemanagergui_qt.h"

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;


#include "gfcrenderparams.h"


#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

gfcPlateManager::gfcPlateManager() : myGUI(nullptr) {
    plates.resize(GFC_MAX_PLATES);
    activeQuad=0;
    framingMode=FRAMINGSINGLE_ID;
    showHelp=false;
    helpMessage="\n\n\nQuickHelp\n(toggle on/off with 'h')\n-------------\n\nVIEWPORT LAYOUTS\nSingle - ctrl+1\nSide by Side - ctrl+2\nTop and Bottom - ctrl+3\n2x2 - ctrl+4\n\nGUI\nToggle Fullscreen Modes - Ctrl+F\nHide Control Bar and Menu - Ctrl+Alt+F\n\nPLAYBACK\nPlay/Pause - Space\nPlay Direction - < or >\nBack/Forward one frame - x/c\nFirst/Last Frame - z/v\nSet Timeline IN Point - i (+Shift to reset, +Alt to start loading there)\nSet Timeline OUT Point - o (+Shift to reset)\nLoad from this point on- Alt+LMB ( on the track or the timeline to load all tracks from this point on)\n\nTRANSFORMS\nFit to screen - f (+Alt Fit all to screen)\nPan - LMB drag (+Alt for gang zoom)\nZoom - Mouse Wheel or Ctrl+LMB drag (+Alt for gang zoom)\n\nOTHERS\nColor Picker: Ctrl+RMB\n\n\nREMOTE SESSIONS\nRemote Pointer - RMB drag\nEnter/Exit Chat Mode - y/Escape\nShow Chat - Ctrl+y";
    stopRendering=true;
    prevW=1024;
    prevH=768;

    showLutPreview=false;
    showLutUniform=false;
    showLutChoice=0;
    showLutscale=1.0;
    showLutTX=0;
    showLutTY=0;


}


gfcPlateManager::~gfcPlateManager() {
    delete myGUI;
}

void gfcPlateManager::drawPlate(int whichOne) {
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::drawPlate: requested plate out of range\n");
        return;
    }

    plates[whichOne].draw();
}

/**
 * This method is in charge of setting up the viewports and drawing each of it's plates to where they belong, need to know the size of the window
 it is drawing to in order to correctly setup the viewports.
 */
void gfcPlateManager::drawPlates(int w, int h, bool resized) {
    //printf("Framing Mode: %i resized=%i\n",framingMode, resized);
    int currentFrame=1;
    
    if (showLutPreview) {
        if ( resized ) {
            //printf("Resetting projections and viewports!\n");
            glMatrixMode ( GL_PROJECTION );
            glLoadIdentity();
            glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );
        }
        glMatrixMode ( GL_MODELVIEW );
        glLoadIdentity();
        glPushAttrib(GL_VIEWPORT_BIT);
    	glViewport(0,0,w,h);
        
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
        //glTranslatef(-showLutTX,-showLutTY,0);
        gfc_gl_font(FL_HELVETICA, 12);
        textRenderer().setColor(1.0, 1.0, 1.0, 1.0);
		gfc_gl_draw(lutManager.getLUT(showLutChoice).filename, (int)(-w/2.0+10), -10, (int)(w/2.0), (int)(h/2.0), FL_ALIGN_LEFT | FL_ALIGN_TOP);
        lutManager.drawLut(showLutChoice, showLutscale, showLutTX, showLutTY, 0, showLutUniform, w, h);
    
        
        glPopAttrib();

    } else {
        switch (framingMode) {
        default:
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );
            glViewport(0,0,w,h);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glColor3f(1,1,1);
            glPointSize(10);
            glBegin(GL_POINTS);
            glVertex3f(0,0,0);
            glEnd();

            gfc_gl_font(FL_HELVETICA, 15);
            textRenderer().setColor(1, 1, 1, 1);
            gfc_gl_draw("no layout selected", 0.0f, 0.0f);

            break;


        case FRAMINGSINGLE_ID:

            glMatrixMode ( GL_PROJECTION );
            glLoadIdentity();
            glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );

            glMatrixMode ( GL_MODELVIEW );
            glLoadIdentity();

            plates[0].setViewport( 0,0,w,h );
            plates[0].rect.set ( -w /2, -h /2, w,h );

            //printf("Calling plates[0].draw\n");
            plates[0].draw ();
            break;//*/

        case FRAMINGDOUBLE_ID:
            glMatrixMode ( GL_PROJECTION );
            glLoadIdentity();
            glOrtho ( -w /4.0, w /4.0, -h /2.0, h /2.0, -5000.0, 5000.0 );
            glMatrixMode ( GL_MODELVIEW );
            glLoadIdentity();
            plates[0].setViewport( 0,0,w /2,h );

            plates[0].rect.set ( -w /4, -h /2,
                                 w /2,h );
            plates[0].draw (  );


            plates[1].setViewport( ( int ) w /2,0, ( int ) w /2,h );

            plates[1].rect.set ( -w /4, -h /2,
                                 w /2,h );
            plates[1].draw ();

            break;

        case FRAMINGDOUBLEVERT_ID:
            glMatrixMode ( GL_PROJECTION );
            glLoadIdentity();
            glOrtho ( -w /2.0, w /2.0, -h /4.0, h /4.0, -5000.0, 5000.0 );
            glMatrixMode ( GL_MODELVIEW );
            glLoadIdentity();
            plates[1].setViewport( 0,0,w,h /2 );
            plates[1].rect.set ( -w /2, -h /4,
                                 w,h /2 );
            plates[1].draw ();

            plates[0].setViewport( 0, ( int ) h /2,w,h /2 );
            plates[0].rect.set ( -w /2, -h /4,
                                 w,h /2 );

            plates[0].draw ();

            break;

        case FRAMINGQUAD_ID:
            glMatrixMode ( GL_PROJECTION );
            glLoadIdentity();
            glOrtho ( -w /4.0, w /4.0, -h /4.0, h /4.0, -5000.0, 5000.0 );
            glMatrixMode ( GL_MODELVIEW );
            glLoadIdentity();

            plates[0].setViewport( 0,h /2,w /2,h /2 );
            plates[0].rect.set ( -w /4, -h /4,
                                 w /2,h /2 );
            plates[0].draw (  );

            plates[1].setViewport( w /2,h /2,w /2,h /2 );
            plates[1].rect.set ( -w /4, -h /4,
                                 w /2,h /2 );
            plates[1].draw (  );

            plates[2].setViewport( 0,0,w /2,h /2 );
            plates[2].rect.set ( -w /4, -h /4,
                                 w /2,h /2 );
            plates[2].draw (  );

            plates[3].setViewport( w /2,0,w /2,h /2 );
            plates[3].rect.set ( -w /4, -h /4,
                                 w /2,h /2 );
            plates[3].draw (  );

            break;


        }
    }


}

void gfcPlateManager::setFramingMode(int pframingMode) {
    setChanged();
	framingMode=pframingMode;

    int w,h,x,y;

    w=myGUI->getLayoutGroupW();
    h=myGUI->getLayoutGroupH();
    x=myGUI->getLayoutGroupX();
    y=myGUI->getLayoutGroupY();

    switch ( framingMode ) {
    case FRAMINGSINGLE_ID: {

		myGUI->setLayoutChoice(0);
		
		this->setActiveQuad(0);
		
        plates[0].myGUI->setGroupVisible(1);
        plates[1].myGUI->setGroupVisible(0);
        plates[2].myGUI->setGroupVisible(0);
        plates[3].myGUI->setGroupVisible(0);

		plates[0].myGUI->setActiveVisible(1);
		plates[1].myGUI->setActiveVisible(0);
		plates[2].myGUI->setActiveVisible(0);
		plates[3].myGUI->setActiveVisible(0);


        //plates[0].myGUI->setGroupPosition(x+w/4,y+h/4);
		
    }
    break;

    case FRAMINGDOUBLE_ID: {

		myGUI->setLayoutChoice(1);

		if (activeQuad==2 || activeQuad==3)
		{
			this->setActiveQuad(0);
		}
		
		plates[0].myGUI->setActiveVisible(1);
		plates[1].myGUI->setActiveVisible(1);
		plates[2].myGUI->setActiveVisible(0);
		plates[3].myGUI->setActiveVisible(0);

		//plates[0].myGUI->enableActiveWidget();
		
        /*plates[0].myGUI->setGroupVisible(1);
        plates[1].myGUI->setGroupVisible(1);
        plates[2].myGUI->setGroupVisible(0);
        plates[3].myGUI->setGroupVisible(0);*/

       /* plates[0].myGUI->setGroupPosition(x+2,y+h/4);
        plates[1].myGUI->setGroupPosition(x+w/2+2,y+h/4);*/
    }
    break;


    case FRAMINGDOUBLEVERT_ID: {

		myGUI->setLayoutChoice(2);

		if (activeQuad==2 || activeQuad==3)
		{
			this->setActiveQuad(0);
		}

		plates[0].myGUI->setActiveVisible(1);
		plates[1].myGUI->setActiveVisible(1);
		plates[2].myGUI->setActiveVisible(0);
		plates[3].myGUI->setActiveVisible(0);

       /* plates[0].myGUI->setGroupVisible(1);
        plates[1].myGUI->setGroupVisible(1);
        plates[2].myGUI->setGroupVisible(0);
        plates[3].myGUI->setGroupVisible(0);*/

       /* plates[0].myGUI->setGroupPosition(x+w/4,y+2);
        plates[1].myGUI->setGroupPosition(x+w/4,y+h/2+2);*/
    }
    break;

    case FRAMINGQUAD_ID: {

		myGUI->setLayoutChoice(3);
		
		plates[0].myGUI->setActiveVisible(1);
		plates[1].myGUI->setActiveVisible(1);
		plates[2].myGUI->setActiveVisible(1);
		plates[3].myGUI->setActiveVisible(1);

       /* plates[0].myGUI->setGroupVisible(1);
        plates[1].myGUI->setGroupVisible(1);
        plates[2].myGUI->setGroupVisible(1);
        plates[3].myGUI->setGroupVisible(1);

        plates[0].myGUI->setGroupPosition(x+2,y+2);
        plates[1].myGUI->setGroupPosition(x+w/2+2,y);*/
    }
    break;
    }

    myGUI->redrawLayoutGroup();

    networkManager.notifyEvent(GFCNETEVENT_OTHER);

}

void gfcPlateManager::setActiveQuad(int quad)
{
	if (quad>=0 && quad<4)
	{
		activeQuad=quad;

		for (int i=0;i<plates.size();i++)
		{
			plates[i].myGUI->setGroupVisible(quad==i);
			plates[i].myGUI->setActive(quad==i);
		}
	}

}

int gfcPlateManager::getActiveQuad()
{
	return activeQuad;
}

bool gfcPlateManager::getChanged()
{
	if (changed)
	{
		changed=false;
		return true;
	} 
	else
	{
		return false;
	}
	
}


void gfcPlateManager::panPlate(int whichOne, float panX, float panY) {
    
	setChanged();
    if(showLutPreview)
    {
     showLutTX+=panX;
     showLutTY+=panY;
     return;
    }
    
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::drawPlate: requested plate out of range\n");
        return;
    }

    plates[whichOne].panPlate(panX,panY);
    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::zoomPlate(int whichOne, float zoom) {
    setChanged();
    if(showLutPreview)
    {
     showLutscale+=zoom;
     return;
    }
    
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::drawPlate: requested plate out of range\n");
        return;
    }

    plates[whichOne].zoomPlate(zoom);
    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::updateAnimations()
{
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].updateAnimations();


	static float previousOpacity=0;
	feedbackMessageOpacity-=playbackManager.getTimestep();
	feedbackMessageOpacity=max(feedbackMessageOpacity,0.0);
	// Redraw on ANY change, including the final fade to 0 — otherwise the last
	// faint frame lingers until the next forced repaint (the tick idles once
	// hasActiveAnimations() goes false).
	if (previousOpacity!=feedbackMessageOpacity)
	{
		setChanged();
	}
	previousOpacity=feedbackMessageOpacity;
	
	
	
}

void gfcPlateManager::zoomAllPlates(float zoom) {
	setChanged();
    if(showLutPreview)
    {
     showLutscale+=zoom;
     return;
    }
	
    for (int i=plates.size()-1;i>=0;i--)
        plates[i].zoomPlate(zoom);

    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::panAllPlates(float panX, float panY) {
    setChanged();
    if(showLutPreview)
    {
     showLutTX+=panX;
     showLutTY+=panY;
     return;
    }
    
    for (int i=plates.size()-1;i>=0;i--)
        plates[i].panPlate(panX,panY);

    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::scrollLUT(int whichOne, int direction)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::scrollLUT: requested plate out of range\n");
		return;
	}

	plates[whichOne].scrollLUT(direction);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}

void gfcPlateManager::setGamma(int whichOne, float value, int relative)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setGamma: requested plate out of range\n");
		return;
	}

	plates[whichOne].setGamma(value,relative);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setExposure(int whichOne, float value, int relative){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setExposure: requested plate out of range\n");
		return;
	}

	plates[whichOne].setExposure(value,relative);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setBrightness(int whichOne, float value, int relative)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setBrightness: requested plate out of range\n");
		return;
	}

	plates[whichOne].setBrightness(value,relative);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setContrast(int whichOne, float value, int relative)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setContrast: requested plate out of range\n");
		return;
	}

	plates[whichOne].setContrast(value,relative);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setSaturation(int whichOne, float value, int relative)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setSaturation: requested plate out of range\n");
		return;
	}

	plates[whichOne].setSaturation(value,relative);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}

void gfcPlateManager::setGammaAll( float value, int relative)
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].setGamma(value, relative);

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setExposureAll( float value, int relative)
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].setExposure(value, relative);

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setBrightnessAll( float value, int relative)
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].setBrightness(value, relative);

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setContrastAll( float value, int relative)
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].setContrast(value, relative);

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}
void gfcPlateManager::setSaturationAll( float value, int relative)
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].setSaturation(value, relative);

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}

void gfcPlateManager::resetColorCorrection(int whichOne)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::resetColorCorrection: requested plate out of range\n");
		return;
	}

	plates[whichOne].resetColorCorrection();
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
	
}

void gfcPlateManager::resetAllColorCorrections()
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--)
		plates[i].resetColorCorrection();

	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}

void gfcPlateManager::resetAllPlates() {
	setChanged();
    for (int i=plates.size()-1;i>=0;i--)
        plates[i].resetTransforms();

    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::resetPlate(int whichOne) {
    setChanged();
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::drawPlate: requested plate out of range\n");
        return;
    }

    plates[whichOne].resetTransforms();
    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::initializeWidgets() {
    // Qt: each plate gets a stateful gfcPlateGUI_Qt. Widgets push state
    // into it via setters (no Fl_Widget pointers to assign). The plate
    // manager itself uses gfcPlateManagerGUI_Qt for layout-mode tracking.
    // Default plate i to track i (matches the FLTK pairing of
    // plate 0↔track A, plate 1↔track B, …) so getFrameAndSequence picks
    // up sequences[i] instead of trackManager's dummy sequence.
    for (int i = (int)plates.size() - 1; i >= 0; --i) {
        auto* g = new gfcPlateGUI_Qt;
        g->setPlateIndex(i);
        plates[i].myGUI = g;
        plates[i].quadID = i;
        // setTrack writes both gfcPlate::track AND myGUI->trackChoice_,
        // so the plate's renderer and the plate-card's combo agree from
        // launch. Earlier this only set the GUI side; gfcPlate::track
        // stayed 0 for every plate, so all four cards rendered track A.
        plates[i].setTrack(i);
    }
    myGUI = new gfcPlateManagerGUI_Qt;
}

gfcPlateGUI* gfcPlateManager::getPlateGUI(int whichOne) {
    if (whichOne < 0 || whichOne >= (int)plates.size()) return nullptr;
    return plates[whichOne].myGUI;
}

int gfcPlateManager::getPlateAtPosition(int vx, int vy, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    // Mirror drawPlates' viewport partitioning. The cases below match
    // the rectangles set up there per framingMode. Coordinates come in
    // top-down (Qt-style); each branch picks the plate whose viewport
    // covers (vx, vy).
    switch (framingMode) {
        case FRAMINGSINGLE_ID:
            return 0;
        case FRAMINGDOUBLE_ID:
            // Split horizontally: left = plate 0, right = plate 1.
            return (vx < w / 2) ? 0 : 1;
        case FRAMINGDOUBLEVERT_ID:
            // Split vertically (drawPlates puts plate 0 on top).
            return (vy < h / 2) ? 0 : 1;
        case FRAMINGQUAD_ID:
            // 2x2: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
            if (vx < w / 2 && vy < h / 2) return 0;
            if (vx >= w / 2 && vy < h / 2) return 1;
            if (vx < w / 2 && vy >= h / 2) return 2;
            return 3;
        default:
            return 0;
    }
}

void gfcPlateManager::setPlateShowPreview(int whichOne, bool value) {
    if (whichOne < 0 || whichOne >= (int)plates.size() || !plates[whichOne].myGUI) {
        return;
    }
    if (auto* g = dynamic_cast<gfcPlateGUI_Qt*>(plates[whichOne].myGUI)) {
        g->setShowPreview(value);
    }
}

void gfcPlateManager::updateAllGUILUTWidgets()
{
	for (int i=plates.size()-1;i>=0;i--) {
		int prevValue=plates[i].myGUI->getLUT();
		plates[i].myGUI->clearLUTs();
		plates[i].myGUI->addLUTOption("No LUT");
		std::vector< std::string> lutNames=lutManager.getAllNames();
		for (int j=0; j<lutNames.size();j++)
		{
			plates[i].myGUI->addLUTOption(lutNames[j]);
		}
		plates[i].myGUI->setLUT(prevValue);
	}
	
}

void gfcPlateManager::updateColorCorrectionsFromGUI(){
	  setChanged();
	//update the plates
	for (int i=plates.size()-1;i>=0;i--) {
		plates[i].updateColorCorrectionValuesFromGUI();
	}
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
}

void gfcPlateManager::updateTransformationsFromGUI()
{
	setChanged();
	//update the plates
	for (int i=plates.size()-1;i>=0;i--) {
		plates[i].updateTransformationValuesFromGUI();
	}
	networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}


void gfcPlateManager::updatePlatesFromGUI() {
    setChanged();
    for (int i = (int)plates.size() - 1; i >= 0; --i) {
        plates[i].updateValuesFromGUI();
    }
    // Don't touch framingMode or activeQuad — see header comment.
    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::updateAllFromGUI() {
    setChanged();

	//update the layout
	if (myGUI)
	{
		setFramingMode(myGUI->getLayoutChoice()+FRAMINGSINGLE_ID);
	}
	
	
	
	//update the plates
	for (int i=plates.size()-1;i>=0;i--) {
        plates[i].updateValuesFromGUI();
		if(plates[i].getActiveFromGUI())
		{
			setActiveQuad(i);
		}
    }

    networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
	networkManager.notifyEvent(GFCNETEVENT_COLOR);
    networkManager.notifyEvent(GFCNETEVENT_OTHER);

}



int gfcPlateManager::getFramingMode() {
    return this->framingMode;
}

void gfcPlateManager::fitToViewportAll()
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--) {
		plates[i].fitToViewport();
	}
	networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::toggleFlip(int whichOne){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleFlip: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleFlip();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);

}
void gfcPlateManager::toggleFlop(int whichOne)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleFlop: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleFlop();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::toggleFlipAll()
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--) {
		plates[i].toggleFlip();
	}
	networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}

void gfcPlateManager::toggleFlopAll()
{
	setChanged();
	for (int i=plates.size()-1;i>=0;i--) {
		plates[i].toggleFlop();
	}
	networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);
}
void gfcPlateManager::fitToViewport(int whichOne)
{
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::fitToViewport: requested plate out of range\n");
		return;
	} 
	
	plates[whichOne].fitToViewport();
	networkManager.notifyEvent(GFCNETEVENT_TRANSFORMS);

}

void gfcPlateManager::setChannelR(int whichOne, int value){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setChannelR: requested plate out of range\n");
		return;
	}

	plates[whichOne].setChannelR(value);
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::setChannelG(int whichOne, int value){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setChannelG: requested plate out of range\n");
		return;
	}

	plates[whichOne].setChannelG(value);
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::setChannelB(int whichOne, int value){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setChannelB: requested plate out of range\n");
		return;
	}

	plates[whichOne].setChannelB(value);
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::setChannelA(int whichOne, int value){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setChannelA: requested plate out of range\n");
		return;
	}

	plates[whichOne].setChannelA(value);
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::toggleChannelR(int whichOne){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleChannelR: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleChannelR();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}
void gfcPlateManager::toggleChannelG(int whichOne){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleChannelG: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleChannelG();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}
void gfcPlateManager::toggleChannelB(int whichOne){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleChannelB: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleChannelB();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}
void gfcPlateManager::toggleChannelA(int whichOne){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::toggleChannelA: requested plate out of range\n");
		return;
	}

	plates[whichOne].toggleChannelA();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlateManager::toggleTextMode(int whichOne) {
    
	setChanged();   
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::toggleTextMode: requested plate out of range\n");
        return;
    }

    plates[whichOne].toggleTextMode();
    textModeReset=true;
}

void gfcPlateManager::toggleTextModeAll() {
    setChanged();
	for (int i=plates.size()-1;i>=0;i--)
        plates[i].toggleTextMode(textModeReset); //whenever textModeAll reaches 0, we reset the

    textModeReset=false;
}


void gfcPlateManager::toggleHistogramMode(int whichOne) {
    
	setChanged();   
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::toggleHistogramMode: requested plate out of range\n");
        return;
    }

    plates[whichOne].toggleHistogramMode();
    histogramModeReset=true;
}

void gfcPlateManager::toggleHistogramModeAll() {
    setChanged();
	for (int i=plates.size()-1;i>=0;i--)
        plates[i].toggleHistogramMode(histogramModeReset); //whenever textModeAll reaches 0, we reset the

    histogramModeReset=false;
}

void gfcPlateManager::toggleHelp() {
    setChanged();
	showHelp=!showHelp;



}

void gfcPlateManager::drawHelp(int w, int h) {



}


void gfcPlateManager::addFXToPlate(int whichOne, gfcFX theFX) {
    setChanged();
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::drawPlate: requested plate out of range\n");
        return;
    } else {

		//TODO: add to the recent FXs;
		sett.addToRecentFXs(theFX.name);

        plates[whichOne].fxStack.addFX(theFX);
	this->clearHistogramCache(whichOne);
	
        //send fx
        gfcNetFXAddInfo info;
        info.id.hash=theFX.md5Hash;
        info.id.quadID=whichOne;
        networkManager.sendFXAddMessage(info);
    }

}

void gfcPlateManager::setFXWidgetValue(int whichOne, int fxIndex,
                                       const std::string& groupName,
                                       const std::string& widgetName,
                                       float value) {
    setChanged();
    if (whichOne < 0 || whichOne >= (int)plates.size()) {
        printf("gfcPlateManager::setFXWidgetValue: requested plate out of range\n");
        return;
    }
    gfcFXStack* stack = &plates[whichOne].fxStack;
    stack->setWidgetValue(fxIndex, groupName, widgetName, value);
    this->clearHistogramCache(whichOne);

    // Broadcast the live param edit to remote peers (mirrors the throttled
    // COLOR/TRANSFORMS streams). sendFXAttribMessage no-ops when not in a
    // session, but skip building the message entirely when solo. The receiver
    // (gfcFXStack::processNetFXAttribInfo) keys off attribType to pick which
    // field it reads, so fill the one matching this widget's type.
    if (!networkManager.getConnected())
        return;

    gfcNetFXAttribInfo info;
    info.id.quadID    = whichOne;
    info.id.index     = fxIndex;
    info.groupName    = groupName;
    info.variableName = widgetName;
    info.theFloat     = 0.0f;
    info.theInt       = 0;
    info.attribType   = stack->getWidgetType(fxIndex, groupName, widgetName);

    switch (info.attribType) {
    case FX_GUI_FLOAT:
        info.theFloat = value;
        break;
    case FX_GUI_BOOL:
    case FX_GUI_CHOICE:
    case FX_GUI_TEXTURE:
        info.theInt = (int)value;
        break;
    case FX_GUI_LUT:
    case FX_GUI_CUBE: {
        // The panel stores the GLOBAL lutManager index as the value; the wire
        // carries the LUT's name (getAllNames is indexed by that same global
        // index, and the receiver maps name -> index via getLutIndexByName).
        std::vector<std::string> names = lutManager.getAllNames();
        int idx = (int)value;
        if (idx >= 0 && idx < (int)names.size())
            info.lutOrCube = names[idx];
        break;
    }
    default:
        return;  // unknown widget type — nothing meaningful to send
    }

    networkManager.sendFXAttribMessage(info);
}

gfcFXStack * gfcPlateManager::getFXStack(int whichOne) {
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::getFXStack: requested plate out of range\n");
        return NULL;
    } else {
        return &(plates[whichOne].fxStack);
    }
}

void gfcPlateManager::setFXStack(gfcFXStack theStack, int whichOne)
{
	setChanged();
	 if (whichOne>=plates.size()) {
        printf("gfcPlateManager::getFXStack: requested plate out of range\n");
        return;
    } else {
        plates[whichOne].fxStack=theStack;

		//send notification that a stack was loaded
		gfcNetFXStackMessage message;
		message.quadID=whichOne;
		XMLNode node = XMLNode::createXMLTopNode("Stack");
		theStack.saveStackToNode(node);
		message.theStack=node.createXMLString();

		networkManager.sendFXStackMessage(message);    
    }
    
	
}

int gfcPlateManager::handleFXGUICB(int whichOne, void * widgetHandle, void * data) {
    setChanged();
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::handleFXGUICB: requested plate out of range\n");
        return 0;

    } else {
    	this->clearHistogramCache(whichOne);
        return plates[whichOne].fxStack.handleGUICB(widgetHandle, data);
    }


}

int gfcPlateManager::getTrackOnPlate(int whichOne) {
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::getTrackOnPlate: requested plate out of range\n");
        return 0;

    } else {
        return plates[whichOne].myGUI->getSequenceID();
    }
}

void gfcPlateManager::setTrackOnPlate(int whichOne, int theTrack)
{
	if (whichOne>=plates.size()) {
        printf("gfcPlateManager::setTrackOnPlate: requested plate out of range\n");

    } else {
        plates[whichOne].setTrack(theTrack);
        
        networkManager.notifyEvent(GFCNETEVENT_OTHER);
    }
        
}


void gfcPlateManager::draw(int w, int h, bool resized) {
    //DRAW PLATES

    if (w==0 || h==0) {
        w=prevW;
        h=prevH;
    } else {
        prevW=w;
        prevH=h;
    }
	
    drawPlates(w,h,resized);

	
	

    //DRAW HELP OR OTHER 2D OVERLAYS
    if (showHelp || feedbackMessageOpacity>0.0) {
	
		//SETUP 2D VIEWPORT
		glPushAttrib(GL_ALL_ATTRIB_BITS);
        //printf("Resetting projections and viewports!\n");
        glMatrixMode ( GL_PROJECTION );
        glPushMatrix();
        glLoadIdentity();
        glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );
        //glOrtho(0,300,0,300,-1,1);


        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
        glMatrixMode ( GL_MODELVIEW );
        glPushMatrix();
        glLoadIdentity();
        glViewport ( 0,0,w,h );
		

		//DRAW WHATEVER NEEDS TO BE DRAWN
		if (feedbackMessageOpacity>0.0)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			gfc_gl_font(FL_HELVETICA, sett.feedbackMessageSize);

			float theOpacity=min(feedbackMessageOpacity,1.0)*0.5;

			//draw a background
			int textWidth=w/2; //the box should not be wider than 1/2 of the viewport
			int textHeight=h/10;

			gfc_gl_measure(feedbackMessage.c_str(), textWidth, textHeight);
			// gfc_gl_measure returns LOGICAL pixels, but this overlay is laid
			// out and drawn in the viewport's PHYSICAL-pixel ortho (draw() is
			// called with framebuffer dimensions). On Retina the two differ by
			// dpiScale, so without this the box is ~half the text's real size —
			// the message then wraps to two lines and runs off the edge. Scale
			// the measurement up to physical pixels to match the ortho.
			const float uiScale = gfc_gl_dpiscale();
			textWidth  = (int)(textWidth  * uiScale);
			textHeight = (int)(textHeight * uiScale);

			textWidth*=1.2;
			textHeight*=1.2;

			int textPosx=w/2.0-textWidth-5;
			int textPosy=-h/2.0+5;

			//textPosx=textPosy=0;


			//draw bg
			
			float bgColor=GFC_WIDGET_DARK_TEXT_COLOR/255.0;
			glColor4f(bgColor,bgColor,bgColor,theOpacity);
			glBegin(GL_QUADS);

				glVertex2f(textPosx,textPosy);
				glVertex2f(textPosx+textWidth,textPosy);
				glVertex2f(textPosx+textWidth,textPosy+textHeight);
				glVertex2f(textPosx,textPosy+textHeight);

			glEnd();

			glLineWidth(1);
			float bgBorderColor=GFC_WIDGET_LIGHT_TEXT_COLOR/255.0;
			glColor4f(bgBorderColor,bgBorderColor,bgBorderColor,theOpacity);
			glBegin(GL_LINE_LOOP);
				glVertex2f(textPosx,textPosy);
				glVertex2f(textPosx+textWidth,textPosy);
				glVertex2f(textPosx+textWidth,textPosy+textHeight);
				glVertex2f(textPosx,textPosy+textHeight);
			glEnd();

			//draw the text
			textRenderer().setColor(1.0, 1.0, 1.0, theOpacity);
			gfc_gl_draw(feedbackMessage.c_str(),
				textPosx, textPosy, textWidth, textHeight, FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
			
						

			//printf("Drawing message: %s\n x:%i, y:%i width:%i, height:%i, opacity: %f",feedbackMessage.c_str(),textPosx,textPosy,textWidth,textHeight,feedbackMessageOpacity);
		}
		if (showHelp)
		{
			//printf("SHOWING HELP\n");
			gfc_gl_font(FL_HELVETICA, 15);
			textRenderer().setColor(1, 1, 1, 1);
			gfc_gl_draw(helpMessage.c_str(),
					-w/2, -h/2, w, h,
					FL_ALIGN_CENTER | FL_ALIGN_WRAP);
		}




        glPopMatrix();
        glMatrixMode ( GL_PROJECTION );
        glPopMatrix();
        glMatrixMode ( GL_MODELVIEW );
        glPopMatrix();
        glPopAttrib();
    }

    //DRAW DEMO OVERLAY

}





void gfcPlateManager::storePointerInfo(gfcNetRemotePointerInfo info) {
    //printf("Storing?\n");
    for (int i=plates.size()-1;i>=0;i--) { //store the coords in each plate
        if (info.quadID==i) {
            plates[i].storePointerInfo(info);
        } else {
            plates[i].removePointerInfo(info);
        }
    }
}



Vec3D gfcPlateManager::getCursorPositionIn2DSpace(int px, int py, int whichOne) {
    Vec3D tmpVec;
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::getCursorPositionIn2DSpace: requested plate out of range\n");
    } else {
        tmpVec=plates[whichOne].getCursorPositionIn2DSpace(px, py);
    }
    return tmpVec;
}

std::vector< gfcNetTransformationInfo > gfcPlateManager::getTransformations() {
    std::vector< gfcNetTransformationInfo  > result;
    int size=plates.size();
    for (int i=0;i<size;i++) { //get transformations for each plate
        result.push_back(plates[i].getTransformations());
    }
    return result;
}

void gfcPlateManager::setTransformations(std::vector< gfcNetTransformationInfo > transformations) {
    setChanged();
	int size=plates.size();
    int tsize=transformations.size();
    for (int i=0;i<size && i<tsize;i++) { //set transformations for each plate
        plates[i].setTransformations(transformations[i]);
    }
}

std::vector<gfcNetPlateColorCorrectionInfo>  gfcPlateManager::getColorCorrections()
{
	std::vector< gfcNetPlateColorCorrectionInfo  > result;
	int size=plates.size();
	for (int i=0;i<size;i++) { //get color corrections for each plate
		result.push_back(plates[i].getPlateColorCorrectionInfo());
	}
	return result;
}

void  gfcPlateManager::setColorCorrections(std::vector<gfcNetPlateColorCorrectionInfo> corrections)
{
	setChanged();
	int size=plates.size();
	int csize=corrections.size();
	for (int i=0;i<size && i<csize;i++) { //set transformations for each plate
		plates[i].setPlateColorCorrectionInfo(corrections[i]);
	}
}

void gfcPlateManager::setLUTAll(int lutIndex)
{
	setChanged();
	int size=plates.size();
	for (int i=0;i<size;i++) { //set LUT for each plate
		plates[i].setLUT(lutIndex);
	}
}

void gfcPlateManager::setLUT(int whichOne, int lutIndex)
{
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setLUT: requested plate out of range\n");
	} else {
		plates[whichOne].setLUT(lutIndex);
	}
}

void gfcPlateManager::setLUTByName(int whichOne, std::string lutName)
{
	setLUT(whichOne,lutManager.getLutIndexByName(lutName));
	
}



gfcNetPlateColorCorrectionInfo gfcPlateManager::getColorCorrection(int whichOne){
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::getColorCorrection: requested plate out of range\n");
		gfcNetPlateColorCorrectionInfo emptyCC;
		return emptyCC;
	} else {
		return plates[whichOne].getPlateColorCorrectionInfo();
	}
}

void gfcPlateManager::setColorCorrection(int whichOne, gfcNetPlateColorCorrectionInfo ccInfo){
	setChanged();
	if (whichOne>=plates.size()) {
		printf("gfcPlateManager::setColorCorrection: requested plate out of range\n");
		return;
	} else {
		plates[whichOne].setPlateColorCorrectionInfo(ccInfo);
		networkManager.notifyEvent(GFCNETEVENT_COLOR);  
	}	
}

std::vector< gfcNetPlateStateInfo > gfcPlateManager::getPlateStateInfo() {
    std::vector< gfcNetPlateStateInfo > result;
    int size=plates.size();
    for (int i=0;i<size;i++) { //get transformations for each plate
        result.push_back(plates[i].getPlateStateInfo());
    }
    return result;
}

void gfcPlateManager::setPlateStateInfo(std::vector< gfcNetPlateStateInfo > states) {
    setChanged();
    int size=plates.size();
    int ssize=states.size();
    for (int i=0;i<size && i<ssize;i++) { //set transformations for each plate
        plates[i].setPlateStateInfo(states[i]);
    }
}

void gfcPlateManager::processNetFXAttribInfo(gfcNetFXAttribInfo &info) {
    setChanged();
    if (info.id.quadID>=plates.size()) {
        printf("gfcPlateManager::processNetFXAttribInfo: requested plate out of range\n");
    } else {
        plates[info.id.quadID].processNetFXAttribInfo(info);
    }
}

void gfcPlateManager::processNetFXCommonInfo(gfcNetFXCommonInfo &info) {
     setChanged();
    if (info.id.quadID>=plates.size()) {
        printf("gfcPlateManager::processNetFXCommonInfo: requested plate out of range\n");
    } else {
        plates[info.id.quadID].processNetFXCommonInfo(info);
    }
}

void gfcPlateManager::processNetFXStackMessage(gfcNetFXStackMessage &message)
{
	gfcFXStack tmpStack;
	tmpStack.loadStackFromString(message.theStack);
	this->setFXStack(tmpStack,message.quadID);
}



void gfcPlateManager::setRemotePointerOptions(int pfontSize, int psize, bool pfade, int pfadeDelay, bool ptrail, float ptrailLenght, int pColor) {
    setChanged();
	int size=plates.size();
    for (int i=0;i<size;i++) { //set transformations for each plate
        plates[i].setRemotePointerOptions(pfontSize, psize, pfade, pfadeDelay, ptrail, ptrailLenght, pColor);
    }
}

void gfcPlateManager::setTextDisplayOptions(int pfontSize, float pcolor, float popacity) {
	setChanged();
    int size=plates.size();
    for (int i=0;i<size;i++) { //set transformations for each plate
        plates[i].setTextDisplayOptions(pfontSize, pcolor,popacity);
    }
}

void gfcPlateManager::saveStackToFile(int whichOne, std::string filename) {
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::saveStackToFile: requested plate out of range\n");
    } else {
        plates[whichOne].fxStack.saveStackToFile(filename);
    }
}

void gfcPlateManager::saveStacksToNode(XMLNode & node) {
    //TODO: Create base stack nodes with info on plates and then pass those nodes to the stacks so they write the stacks to them
	for(int i=0;i<GFC_MAX_PLATES;i++)
	{
		XMLNode xStackNode=node.addChild("Stack");
		this->getFXStack(i)->saveStackToNode(xStackNode);
	}
}



void gfcPlateManager::loadStackFromFile(int whichOne, std::string filename) {
	setChanged();
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::loadStackFromFile: requested plate out of range\n");
    } else {

		gfcFXStack tmp;
		std::vector<std::string> result=tmp.loadStackFromFile(filename);
		this->setFXStack(tmp,whichOne);
        if (result.size()>0) {
            for (int i=0; i<result.size(); i++) {
                // Non-FLTK builds don't have a popup wired yet; log instead.
                fprintf(stderr, "FX stack load warning: %s\n",
                        result[i].c_str());
            }
        }

    }
}

void gfcPlateManager::clearFXStack(int whichOne ) {
	setChanged();
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::clearFXStack: requested plate out of range\n");
    } else {
        plates[whichOne].fxStack.clearStack();

		//send notification that a stack was loaded
		gfcNetFXStackMessage message;
		message.quadID=whichOne;
		XMLNode node = XMLNode::createXMLTopNode("Stack");
		plates[whichOne].fxStack.saveStackToNode(node);
		message.theStack=node.createXMLString();

		networkManager.sendFXStackMessage(message);
    }
	
}

std::vector<gfcFXStack> gfcPlateManager::getPlateFXStacks()
{
	std::vector<gfcFXStack> result;
	int size=plates.size();
	for (int i=0;i<size;i++) { //save plate parameters for each plate
		result.push_back(plates[i].fxStack);
	}
	return result;
}

void gfcPlateManager::setPlateFXStacks(std::vector<gfcFXStack> stacks)
{

	std::vector<gfcFXStack> result;
	int size=plates.size();
	for (int i=0;i<size;i++) { //save plate parameters for each plate
		if (i<stacks.size())
		{
			this->setFXStack(stacks[i],i);
		}
		
	}
	
	//TODO: URGENT! send this message too!
}

std::string gfcPlateManager::getPlateFXStacksAsString(){
	XMLNode node=XMLNode::createXMLTopNode("Stacks");
	
	this->saveStacksToNode(node);
	std::string result = node.createXMLString();
	//std::cout<<result<<std::endl;
	return result;
}
void gfcPlateManager::setPlateFXStacksFromString(std::string s){
	
	XMLNode node=XMLNode::parseString(s.c_str());
	int psize=node.nChildNode("Stack");
	int iterator=0;
	std::vector< gfcFXStack > stacks;
	for (int i=0;i<psize;i++) { //load info for each stack
		
		XMLNode stackNode=node.getChildNode("Stack",&iterator);
	
		gfcFXStack tmpStack;
		tmpStack.loadStackFromNode(stackNode);
		stacks.push_back(tmpStack);	
		
	}
	
	this->setPlateFXStacks(stacks);

}

void gfcPlateManager::savePlateSessionParameters(XMLNode &platesNode) {
    int size=plates.size();
    for (int i=0;i<size;i++) { //save plate parameters for each plate
        XMLNode plateNode=platesNode.addChild("plate");
        saveSetting("plateID",i,plateNode);
        plates[i].savePlateSessionParameters(plateNode);
    }
}

void gfcPlateManager::loadPlateSessionParameters(XMLNode & platesNode) {
	setChanged();
    int size=plates.size();
    int psize=platesNode.nChildNode("plate");
    int iterator=0;
	
	for (int i=0;i<size;i++)
	{
		plates[i].fxStack.clearStack();
	}
	

    for (int i=0;i<psize;i++) { //load info for each plate

        XMLNode plateNode=platesNode.getChildNode("plate",&iterator);
        int plateID=readAttributeFromNode<int>("plateID",plateNode,size);

		if (plateID<size){
            plates[plateID].loadPlateSessionParameters(plateNode);
		
			//we now load the stacks out here instead of in the plate's method because we wan't to notify 
			//the network when a session stack was loaded, we could not do that easily from whithin the plate
			//whithout disturbing other things.
			XMLNode stackNode=plateNode.getChildNode("stack");
			gfcFXStack tmpFXStack;
			tmpFXStack.loadStackFromNode(stackNode);
			this->setFXStack(tmpFXStack,plateID);
		}
    }

}


void gfcPlateManager::renderPlate(gfcRenderParams params, std::vector<std::string> *renderNames) {
    stopRendering=false;
    if (params.quadrant> plates.size())
        return;

    gfcPlate* ptrToPlate=&plates[params.quadrant];

    for ( int i=params.from; i<=params.to;i++ ) {

        ptrToPlate->forRender=true;
        playbackManager.setCurrentFrame(i);
        params.frame=i;
        params.filename=CreateRenderFilename ( params );

		if(renderNames)
			renderNames->push_back(params.filename);

        ptrToPlate->renderParams=params;

        ptrToPlate->draw ( );

        ptrToPlate->forRender=false;

        if ( stopRendering) break;
    }

    printf ( "Done rendering\n\a" );
    stopRendering=true;
}

bool gfcPlateManager::hasActiveAnimations()
{
	if (feedbackMessageOpacity > 0.0) return true;
	for (size_t i = 0; i < plates.size(); ++i)
		if (plates[i].hasActiveAnimation()) return true;
	return false;
}

void gfcPlateManager::setFeedbackMessage(std::string theMessage)
{
	feedbackMessage=theMessage;
	feedbackMessageOpacity=sett.feedbackMessageFadeDelay;
}

void gfcPlateManager::abortRender() {
    stopRendering=true;
}

bool gfcPlateManager::isRendering() {
    return !stopRendering;
}


void gfcPlateManager::setForceSingleBufferedFXs(int value) {
	setChanged();
    int size=plates.size();
    for (int i=0;i<size;i++) { //set force single buffered for each plate

        plates[i].forceSingleBufferedFX=value;
    }

}
void gfcPlateManager::setDrawLUTPreview(bool showPreview, bool showUniform, int lutValue) {
	setChanged();
    this->showLutPreview=showPreview;
    this->showLutUniform=showUniform;
    this->showLutChoice=lutValue;
}

void gfcPlateManager::setChanged()
{

	changed=true;

}

void gfcPlateManager::clearHistogramCache(int whichOne)
{
    if (whichOne>=plates.size()) {
        printf("gfcPlateManager::clearHistogramCache: requested plate out of range\n");
        return;
    }

    plates[whichOne].clearHistogramCache();
}

void gfcPlateManager::setHistogramQuality(int whichOne, int quality)
{
	 if (whichOne>=plates.size()) {
        printf("gfcPlateManager::setHistogramQuality: requested plate out of range\n");
        return;
    }

    plates[whichOne].setHistogramQuality(quality);
}

void gfcPlateManager::setHistogramQuality(int quality)
{
    setChanged();
    int size=plates.size();
    for (int i=0;i<size;i++) { //set quality for each plate

        plates[i].setHistogramQuality(quality);
    }
}



void gfcPlateManager::drawForPicking()
{
	//printf("Drawing for picking\n");
		
	//set all plates to the appropriate render mode. 
	int size=plates.size();
    	for (int i=0;i<size;i++) { //set quality for each plate
        	plates[i].setRenderModeSelection(1);
        }
        
        draw();
        
        for (int i=0;i<size;i++) { //set quality for each plate
        	plates[i].setRenderModeSelection(0);
        }
    	

	
	
}

int gfcPlateManager::pickNotify(gfcPickNotifyParameters & params)
{
	/*printf("Click event: %i, at %ix%i (%i, %i), color: %i %i %i\n",params.event, \
				params.x, params.y, \
				params.dx, params.dy, \
				params.pickedColor.colors[0], params.pickedColor.colors[1], params.pickedColor.colors[2]);*/
				
	//plates should be setup as notifees as well, so we don't have to notify them now,
	//TODO: now we should probably do some sort of viewport adjustments here, instead of having the static viewport layouts
	return 0;
}

void gfcPlateManager::registerPlatesAsPickNotifees()
{
	int size=plates.size();
    	for (int i=0;i<size;i++) { //set quality for each plate
        	pickManager.registerNotifee(&plates[i]);
        }
}

void gfcPlateManager::clearAllHistogramCache()
{
    setChanged();
    int size=plates.size();
    for (int i=0;i<size;i++) { //set quality for each plate

        plates[i].clearHistogramCache();
    }
}

gfcFXStack gfcPlateManager::getFavoriteStack(int whichOne)
{
	if(whichOne>=GFC_MAX_FAVORITE_STACKS || whichOne<0 || whichOne>=favoriteStacks.size())
	{
		printf("ERROR: gfcSettings::getFavoriteStack: tried to get an out of range favorite stack %i\n",whichOne);
		gfcFXStack emptyStack;
		return emptyStack;
	}
	else
	{
		if(favoriteStacks.size()!=GFC_MAX_FAVORITE_STACKS)
		{
			gfcFXStack emptyStack;
			favoriteStacks.resize(GFC_MAX_FAVORITE_STACKS,emptyStack);
		}
		return favoriteStacks[whichOne];
	}
}

std::vector< gfcFXStack > gfcPlateManager::getAllFavoriteStacks()
{
	std::vector< gfcFXStack > result;
	for(int i=0;i<favoriteStacks.size();i++)
	{
		result.push_back(getFavoriteStack(i));
	}
	
	return result;
}

void gfcPlateManager::addToFavoriteStacks(gfcFXStack theStack, int whichOne)
{
	if(whichOne>=GFC_MAX_FAVORITE_STACKS || whichOne<0)
	{
		printf("ERROR: gfcSettings::addToFavoriteStacks: tried to add to an out of range favorite stack %i\n",whichOne);
	}
	else
	{
		if(favoriteStacks.size()!=GFC_MAX_FAVORITE_STACKS)
		{
			gfcFXStack emptyStack;
			favoriteStacks.resize(GFC_MAX_FAVORITE_STACKS,emptyStack);
		}
		
		favoriteStacks[whichOne]=theStack;
	}
}

void gfcPlateManager::setFavoriteOnPlate(int whatFavorite, int whatPlate)
{

	setFXStack(getFavoriteStack(whatFavorite),whatPlate);
}

void gfcPlateManager::appendFXStack(gfcFXStack theStack, int whichOne)
{
	if(whichOne>=GFC_MAX_PLATES || whichOne<0)
	{
		printf("ERROR: gfcPlateManager::appendFXStack: tried to append to an out of range favorite stack %i\n",whichOne);
	}
	else
	{		
		gfcFXStack tmp = *getFXStack(whichOne);
		tmp.appendFXStack(theStack);
		setFXStack(tmp,whichOne);
	}
}

void gfcPlateManager::appendFavoriteOnPlate(int whatFavorite)
{
	appendFXStack(getFavoriteStack(whatFavorite),activeQuad);
}


void gfcPlateManager::setFavoriteOnPlate(int whatFavorite)
{

	setFavoriteOnPlate(whatFavorite,activeQuad);
}

void gfcPlateManager::saveFavoriteFromPlate(int whatFavorite, int whatPlate)
{
	addToFavoriteStacks(*getFXStack(whatPlate),whatFavorite);
}

void gfcPlateManager::saveFavoriteFromPlate(int whatFavorite)
{
	saveFavoriteFromPlate(whatFavorite,activeQuad);
}

void gfcPlateManager::saveFavoriteStacksToNode(XMLNode & node)
{
	for(int i=0;i<favoriteStacks.size();i++)
	{
		XMLNode xFavoriteNode=node.addChild("favoriteStack");
		gfcFXStack tmpStack = getFavoriteStack(i);
		tmpStack.saveStackToNode(xFavoriteNode);
	}
	
}

void gfcPlateManager::loadFavoriteStacksFromNode(XMLNode & xFavoriteStacksNode)
{

	int numFavoriteStacks=xFavoriteStacksNode.nChildNode ( "favoriteStack" );
    	int xmlFavoriteStacksIter=0;
    	printf ( "No. of FavoriteStacks: %i\n",numFavoriteStacks );
    	for ( int i=0;i<numFavoriteStacks;i++ ) {
        	XMLNode tmp=xFavoriteStacksNode.getChildNode ( "favoriteStack",&xmlFavoriteStacksIter );
        	gfcFXStack tmpStack;
        	int tmpIter=0;
        	tmpStack.loadStackFromNode(tmp,&tmpIter);
        	addToFavoriteStacks(tmpStack,i);
    	}

}

gfcNetPlateColorCorrectionInfo gfcPlateManager::getFavoriteColorCorrection(int whichOne){
	if(whichOne>=GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS || whichOne<0 || whichOne>=favoritesColorCorrection.size())
	{
		printf("ERROR: gfcSettings::getFavoriteColorCorrection: tried to add to an out of range favorite stack %i\n",whichOne);
		gfcNetPlateColorCorrectionInfo emptyCC;
		return emptyCC;
	}
	else
	{
		if(favoritesColorCorrection.size()!=GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS)
		{
			gfcNetPlateColorCorrectionInfo emptyCC;
			favoritesColorCorrection.resize(GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS,emptyCC);
		}

		return favoritesColorCorrection[whichOne];
	}
}
void gfcPlateManager::addToFavoritesColorCorrection(gfcNetPlateColorCorrectionInfo theCC, int whichOne){
	if(whichOne>=GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS || whichOne<0)
	{
		printf("ERROR: gfcSettings::addToFavoriteStacks: tried to add to an out of range favorite stack %i\n",whichOne);
	}
	else
	{
		if(favoritesColorCorrection.size()!=GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS)
		{
			gfcNetPlateColorCorrectionInfo emptyCC;
			favoritesColorCorrection.resize(GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS,emptyCC);
		}

		favoritesColorCorrection[whichOne]=theCC;
		printf("Assigned on %i: %s, %f, %f, %f, %f, %f\n",whichOne,theCC.lutName.c_str(),theCC.gamma,theCC.exposure, theCC.brightness, theCC.contrast, theCC.saturation);
	}
}
std::vector<gfcNetPlateColorCorrectionInfo> gfcPlateManager::getAllFavoriteColorCorrections(){
	std::vector< gfcNetPlateColorCorrectionInfo > result;
	for(int i=0;i<favoritesColorCorrection.size();i++)
	{
		result.push_back(getFavoriteColorCorrection(i));
	}

	return result;	
}
void gfcPlateManager::saveFavoriteColorCorrectionsToNode(XMLNode &node){
	for(int i=0;i<favoritesColorCorrection.size();i++)
	{
		gfcNetPlateColorCorrectionInfo tmpCC = getFavoriteColorCorrection(i);
		tmpCC.saveToNode(node);
	}	
}
void gfcPlateManager::loadFavoriteColorCorrectionsFromNode(XMLNode & node){
	int numFavoriteCC=node.nChildNode ( "CC" );
	int xmlFavoriteColorCorrectionsIter=0;
	printf ( "No. of Favorite CCs: %i\n",numFavoriteCC );
	for ( int i=0;i<numFavoriteCC;i++ ) {
		XMLNode tmp=node.getChildNode ( "CC",&xmlFavoriteColorCorrectionsIter );
		gfcNetPlateColorCorrectionInfo tmpCC;
		tmpCC.loadFromNode(tmp);
		addToFavoritesColorCorrection(tmpCC,i);
	}
}

void gfcPlateManager::setFavoriteColorCorrectionOnPlate(int whatFavorite, int whatPlate){
	setColorCorrection(whatPlate, getFavoriteColorCorrection(whatFavorite));
	this->setFeedbackMessage(std::string("Loaded Favorite Color Correction: ")+ftos(whatFavorite+1,0));
}
void gfcPlateManager::setFavoriteColorCorrectionOnPlate(int whatFavorite){
	setFavoriteColorCorrectionOnPlate(whatFavorite,activeQuad);
}
void gfcPlateManager::saveFavoriteColorCorrectionFromPlate(int whatFavorite, int whatPlate){
	addToFavoritesColorCorrection(getColorCorrection(whatPlate),whatFavorite);
	this->setFeedbackMessage(std::string("Saved Favorite Color Correction: ")+ftos(whatFavorite+1,0));
}
void gfcPlateManager::saveFavoriteColorCorrectionFromPlate(int whatFavorite){
	saveFavoriteColorCorrectionFromPlate(whatFavorite,activeQuad);
}



