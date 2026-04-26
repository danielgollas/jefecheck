#include <glad/glad.h>
#include <functional>


#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#include <functional>
#endif

#include "GlViewport.h"
#include "ui/IEventSystem.h"
#include <stdio.h>
#include "mainWindow.h"
#include "loadWindow.h"
#include "preferencesWindow.h"
#include "fxcontrolwindow.h"
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <math.h>
#include <FL/Fl.H>
#include <FL/Fl_Menu_Window.H>
#include <FL/Fl_Color_Chooser.H>
#include <vector>
#include <string>
//#include "network.h"

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "lutWindow.h"
extern LutWindow lutw;

#include "gfcplaylistwindowwindow.h"
extern PlaylistWindow plw;

#include "fxWindow.h"
extern FXWindow fxw;

#include "fxcontrolwindow.h"
extern FXControlWindow fxControlWindow1;

#include "preferencesWindow.h"
extern PreferencesWindow pw;

#include "renderWindow.h"
extern RenderWindow rw;

#include "remoteWindow.h"
extern RemoteWindow rmw;

extern void* gGLContext;
float timeStep;
int timeLineValue;
std::mutex gGLMutex;
extern MainWindow mw;
extern LoadWindow lw;
extern PreferencesWindow pw;
extern FXControlWindow fxControlWindow1;
extern bool mainWindowExists;
extern bool glReady;
extern int gRangeEnd;
extern int gRangeBegin;
extern bool gOutOfMemory;
extern bool gLoadingMemoryError;
extern std::mutex loadingOutOfRamMutex;
//memory mutex and stuff
extern bool gOutOfMemory;
extern std::mutex gNoMoreRamMutex;
extern std::condition_variable gNoMoreRamCondition;

bool rotateActive=false;
float tmpCount=0;
float gFPS;



extern std::map<std::string,gfcNetRemotePointerInfo> nickNamePointerMap;
bool gResizeTrigger;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

namespace { jefe::ui::IEventSystem& evt() { return jefe::ui::IEventSystem::instance(); } }

void PopupWindow::SizeToText() {
    int W=0, H=0;
    fl_font ( output->labelfont(), output->labelsize() );
    fl_measure ( output->label(), W, H, 0 );
    resize ( x(), y(), W+10, H+25 );                        // +10: leaves +5 margin on all sides
    output->resize ( 0, 15, W+10, H+10 );
    theColorBox->resize ( 0,0,W+10,15);
}

void PopupWindow::setColor ( int r, int g, int b ) {
    theColorBox->color ( fl_rgb_color ( r,g,b ) );
    theColorBox->redraw();
}

int getGFCPICKFLAGSfromFLTK()
{
	int result=0;
	if (evt().isCtrl())
	{
		//printf("control clicked!\n");
		result|= GFC_PICK_MODIFIER_CTRL;
	}
	if (evt().isAlt())
	{
		//printf("alt clicked!\n");
		result|= GFC_PICK_MODIFIER_ALT;
	}
	if (evt().isShift())
	{
		//printf("shift clicked!\n");
		result|= GFC_PICK_MODIFIER_SHIFT;
	}
	if (evt().isMouseButtonDown(jefe::ui::MouseButton::Left))
	{
		
		result|=GFC_PICK_MODIFIER_BUTTON1;
	}
	if (evt().isMouseButtonDown(jefe::ui::MouseButton::Middle))
	{
		
		result|=GFC_PICK_MODIFIER_BUTTON2;
	}
	if (evt().isMouseButtonDown(jefe::ui::MouseButton::Right))
	{
		
		result|=GFC_PICK_MODIFIER_BUTTON3;
	}
	return result;
}

float fps() {
   /* static long baseTime=glutGet ( GLUT_ELAPSED_TIME );
    static long currentTime;
    static int frameCount=0;
    static char fps[15];
    frameCount++;
    currentTime=glutGet ( GLUT_ELAPSED_TIME );
    long timeDelta=currentTime-baseTime;

    if ( timeDelta>500 ) {
        //printf("fps:%f\n",frameCount*1000.0/(double)timeDelta);
        sprintf ( fps,"%.0f",frameCount*1000.0/ ( double ) timeDelta );
        frameCount=0;
        baseTime=currentTime;
    }


    return atof ( fps );*/
	return 0;

}

void TimeStep() {
    static long tsBaseTime=glutGet ( GLUT_ELAPSED_TIME );
    static long currentTime;
    currentTime=glutGet ( GLUT_ELAPSED_TIME );

    timeStep= ( currentTime-tsBaseTime ) /1000.0; //timestep in seconds

    //tsBaseTime=glutGet ( GLUT_ELAPSED_TIME );

}






void IdleFunc ( void* pData ) {
    {

    }
}


void GlViewport::size ( int x, int y, int he, int wh ) { //like glut resize func

    printf ( "Resized to: %i, %i, %i, %i\n",0,0, w(),h() );

    glMatrixMode ( GL_PROJECTION );
    glLoadIdentity();
    glOrtho ( -w() /2.0, w() /2.0, h() /2.0, h() /2.0, -5000.0, 5000.0 );
    glViewport ( 0, 0, w(), h() );

    glMatrixMode ( GL_MODELVIEW );
    glLoadIdentity();

}

/*void GlViewport::resize ( int X,int Y,int W,int H )
{
	if(mainWindowExists)
	{//	resizeAllSldrs();
	((Fl_Gl_Window*)this)->resize(X,Y,W,H);
	}
}*/

void GlViewport::draw() {
    // Initialize GLAD on first draw (GL context must exist)
    static bool gladInitialized = false;
    if (!gladInitialized) {
        if (!gladLoadGL()) {
            fprintf(stderr, "Failed to initialize GLAD\n");
            return;
        }
        gladInitialized = true;
    }

    /*if ( gRendering )
        return;*/

    glClearColor ( sett.bgColor,sett.bgColor,sett.bgColor,1 );
    glPushAttrib ( GL_COLOR_BUFFER_BIT );
    glColorMask ( true,true,true,true ); //make sure we clear all the colors, but restore before drawing
    glClear ( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glPopAttrib();



    /*glEnable ( GL_COLOR_MATERIAL );
    glColor4f ( 1,1,1,1 );*/

    //we need to notify the plate manager if the size of the window has changed. If valid() then the window hasn't resized.
    plateManager.draw(w(),h(), !valid());
    networkManager.draw(w(),h(), !valid());

    return;











}



void GlViewport::setVsync(int value)
{
#ifdef WIN32
		// TODO: implement vsync with platform-specific API (wglSwapIntervalEXT)
#endif

#ifdef linux
		// TODO: implement vsync with GLX_SGI_swap_control or GLX_EXT_swap_control
		
#endif
#ifdef __APPLE__
		CGLSetParameter((_CGLContextObject*)context(),kCGLCPSwapInterval,(int*)&value);
#endif
}


#define BUFSIZE 512
GLuint selectBuf[BUFSIZE];
bool dragging=false; //when cursor moves with button pushed.
bool transformingDragging=false; //when the cursor moves when button 1 is pushed.
bool zooming=false; //when mouse wheel did a zoom. Turns on on wheel events, turns off in transformation sends from client,.


void processHits ( GLint hits, GLuint buffer[], int quad ) {
    unsigned int i, j;
    GLuint names, *ptr, minZ,*ptrNames, numberOfNames;

    //printf ("hits = %d\n", hits);
    ptr = ( GLuint * ) buffer;
    minZ = 0xffffffff;
    for ( i = 0; i < hits; i++ ) {
        names = *ptr;
        ptr++;
        if ( *ptr < minZ ) {
            numberOfNames = names;
            minZ = *ptr;
            ptrNames = ptr+2;
        }

        ptr += names+2;
    }
    //printf ("The closest hit names are ");
    ptr = ptrNames;

    switch ( quad ) {
    case 0:
        // mw.vp->q1.poly.selectedPoint=*ptr;
        break;
    }



    /*for (j = 0; j < numberOfNames; j++,ptr++)
    {
      printf ("%d ", *ptr);

    }

    //printf ("\n");*/

}

void startPicking ( int cursorX, int cursorY, float scale, float h, float w ) {
    printf ( "Inside Start Picking\n" );
    GLint viewport[4];

    glSelectBuffer ( BUFSIZE,selectBuf );
    glRenderMode ( GL_SELECT );

    glMatrixMode ( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();

    glGetIntegerv ( GL_VIEWPORT,viewport );
    gluPickMatrix ( cursorX,viewport[3]-cursorY,
                    10,10,viewport );
    gluOrtho2D ( ( -w ) /scale, ( w ) /scale, ( -h ) /scale, ( h ) /scale );
    glMatrixMode ( GL_MODELVIEW );

    glInitNames();
}



void stopPicking ( int quad ) {

    int hits;

    // restoring the original projection matrix
    glMatrixMode ( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode ( GL_MODELVIEW );
    glFlush();

    // returning to normal rendering mode
    hits = glRenderMode ( GL_RENDER );

    // if there are hits process them
    if ( hits != 0 ) {
        printf ( "Hits > 0!\n" );
        processHits ( hits,selectBuf, quad );
        dragging=true;
    } else {
        printf ( "No hits!\n" );
        switch ( quad ) {
        case 0:
            //mw.vp->q1.poly.selectedPoint=-1;
            break;
        }


        dragging=false;
    }
}

int GlViewport::handle ( int e ) {
    static float zoomSpeed=0.1;
	static int scrubDraggCounter=0;
    int ret = Fl_Gl_Window::handle ( e );
    static int pastedXPos;
    static int pastedYPos;
	static double lastPointerSend=0;
    //printf("Handling Viewport event\n");
    //printf("GLViewport got this event\n");


    /*if ( Fl::belowmouse() ==this )
    	printf ( "OpenGL under mouse\n" );
    else
    	printf("OpenGL NOT under mouse\n");*/

    //return 0;


    switch ( e ) {

    case FL_DND_ENTER:          // return(1) for these events to 'accept' dnd
    case FL_DND_DRAG:
    case FL_DND_RELEASE:
        pastedXPos=evt().mouseX();
        pastedYPos=evt().mouseY();
        return(1);
        break;

        break;

    case FL_ENTER: //need this to "request" the mouse move messages
        return 1;
        break;

    case FL_MOVE: {
        int eventX=evt().mouseX(), eventY=evt().mouseY();



        return 0;
    }
    break;

    case FL_DRAG: {
        int eventX=evt().mouseX(), eventY=evt().mouseY();

        int somethingPicked=pickManager.doPicking(GFC_PICK_EVENT_DRAG, getGFCPICKFLAGSfromFLTK(), eventX, h()-eventY, prevX-eventX,prevY-eventY);
		
		if (!somethingPicked)
		{
		
        if (evt().isMouseButtonDown(jefe::ui::MouseButton::Left)) { //LEFT MOUSE BUTTON
            if ((Fl::get_key ( FL_Shift_L)) || (Fl::get_key ( FL_Shift_R))) {
				//this will scrub, we should map the total timeline lenght to half the width of the viewport. So going middle to edge should scrub the whole thing.
				int tlLenght= playbackManager.getToFrame()-playbackManager.getFromFrame();
				int vpW=this->w();
				int delta=eventX-prevX;
				float prop=(float)vpW/(float)tlLenght;
				scrubDraggCounter+=delta;
				if(prop==0)
				{
				   prop=0.001;
				}
				if(scrubDraggCounter>abs(prop))
				{
					
					playbackManager.oneFrameFwd(abs(scrubDraggCounter/prop));
					scrubDraggCounter-=(int)((scrubDraggCounter/prop))*prop;// leave the remainder in there.
					//printf("scrubDraggCounter left: %i\n",scrubDraggCounter);
				}
				else
				{
					if (scrubDraggCounter<-prop)
					{
						playbackManager.oneFrameRev(abs(scrubDraggCounter/prop));
						scrubDraggCounter-=(int)((scrubDraggCounter/prop))*prop;//prop-scrubDraggCounter;
						//printf("scrubDraggCounter left: %i\n",scrubDraggCounter);
					}
				}


			}
			else
			{
				
			
			if ((Fl::get_key ( FL_Control_L ) || Fl::get_key ( FL_Control_R ))) {
                zoomSpeed=evt().isShift() ?0.001:0.005;
				
                if (evt().isKeyDown(jefe::ui::Key::AltL) || evt().isKeyDown(jefe::ui::Key::AltR)) {
                    plateManager.zoomAllPlates((prevY-eventY) *zoomSpeed);
                } else {
                    plateManager.zoomPlate(startQuad-1,(prevY-eventY) *zoomSpeed);
                }
            } else {
				
				float adjustmentValue=(eventX-prevX)*0.01; 
							
//				printf("adjustmentValue %f\n",adjustmentValue);

				if (evt().isKeyDown(static_cast<jefe::ui::Key>('W')))
				{
					if (evt().isAlt())
					{
						plateManager.setGammaAll(adjustmentValue,1);
					} 
					else
					{
						plateManager.setGamma(plateManager.getActiveQuad(),adjustmentValue,1);
					}
					
				}
				else
				{
					if ( evt().isKeyDown(static_cast<jefe::ui::Key>('E')))
					{
						if (evt().isAlt())
						{
							plateManager.setExposureAll(adjustmentValue,1);
						} 
						else
						{
							plateManager.setExposure(plateManager.getActiveQuad(),adjustmentValue,1);
						}
						
					} 
					else
					{
						if (evt().isKeyDown(static_cast<jefe::ui::Key>('Q')))
						{
							if (evt().isAlt())
							{
								plateManager.setBrightnessAll(adjustmentValue,1);
							
							}
							else
							{
								plateManager.setBrightness(plateManager.getActiveQuad(),adjustmentValue,1);	
							}
						} 
						else
						{
							if (evt().isKeyDown(static_cast<jefe::ui::Key>('D')))
							{
								if (evt().isAlt())
								{
										plateManager.setContrastAll(adjustmentValue,1);
								}
								else
								{
										plateManager.setContrast(plateManager.getActiveQuad(),adjustmentValue,1);
								}
								
							} 
							else
							{
								if (evt().isKeyDown(static_cast<jefe::ui::Key>('S')))
								{
									if (evt().isAlt())
									{
										plateManager.setSaturationAll(adjustmentValue,1);
									} 
									else
									{
										plateManager.setSaturation(plateManager.getActiveQuad(),adjustmentValue,1);
									}
										
								} 
								else
								{									
									
										//handle drag operations with keyboard.

										//if no keys were pressed then just pan
										if (evt().isKeyDown(jefe::ui::Key::AltL) || evt().isKeyDown(jefe::ui::Key::AltR)) {
											plateManager.panAllPlates(prevX-eventX,prevY-eventY);
										} else {
											plateManager.panPlate(startQuad-1,prevX-eventX,prevY-eventY);
										}
									
								}
							}
						}
					}
				}

            }
			}
        }

        if (evt().isMouseButtonDown(jefe::ui::MouseButton::Right)) { //RIGHT MOUSE BUTTON

            if (!lw.loadWindow->visible()) {
                if (evt().isCtrl()) {
                    char tmpPopupText[2048];
                    GLint viewport[4];
                    GLubyte pixel[3];

                    glGetIntegerv ( GL_VIEWPORT,viewport );

//             glReadPixels ( eventX,viewport[3]-eventY,1,1,
//                            GL_RGB,GL_UNSIGNED_BYTE, ( void * ) pixel );

                    glReadBuffer(GL_FRONT);
                    glReadPixels ( eventX,h()-eventY,1,1,
                                   GL_RGB,GL_UNSIGNED_BYTE, ( void * ) pixel );
                    glReadBuffer(GL_BACK);
                    float luminance= 0.2126*pixel[0] + 0.7152*pixel[1] + 0.0722*pixel[2];
                    //printf("Luminance: %f\n",luminance);

                    sprintf ( tmpPopupText,"R: %03d (%.3f)\nG: %03d (%.3f)\nB: %03d (%.3f)\n\nY:%03i (%.3f)\n",pixel[0],pixel[0]/255.0,pixel[1],pixel[1]/255.0,pixel[2],pixel[2]/255.0, ( int ) luminance, luminance/255.0 );

                    popup->text ( tmpPopupText );
                    popup->updatePos();
                    popup->setColor ( pixel[0],pixel[1],pixel[2] );
                }

				static int pointersSent=0;


                if (networkManager.getConnected()) {

					 //lastPointerSend+=playbackManager.getTimestep();
					  if (pointersSent%1000==0) printf("pointersSent: %i\n",pointersSent);
					//if (lastPointerSend>=1.0/60.0)
					{
						lastPointerSend=0; 
					
					pointersSent++;
                    Vec3D tmpVec;
                    int quad=getQuadFromMousePos(eventX,eventY)-1;
                    //calculate the cursor position in actual image space from the mouse position and the quad we are hovering over
                    tmpVec=plateManager.getCursorPositionIn2DSpace(eventX,h()-eventY,quad); //remember to invert the Y coordinate for openGL, also -1 since getQuad gets it in 1 based index, TODO: change the way getQuadFromMousePos returns junk,
                    //NOTE: the Z coord contains the scale!!!
                    //tell networkManager to send remote pointer (it will only send info when we actually are connected and the position is different from the last)
                    gfcNetPointerInfo info;
                    info.quadID=quad;
                    info.x=tmpVec.x;
                    info.y=tmpVec.y;
                    info.scale=tmpVec.z;
                    networkManager.sendPointerInfoMessage(info);
					}
                }

            } else { //if the load window is on, then we move the aoi
                if (evt().isKeyDown(jefe::ui::Key::ShiftL)) {
                    trackManager.getSequence(startQuad-1)->setAOI(0,0,eventX-prevX,prevY-eventY,true);
                } else {
                    trackManager.getSequence(startQuad-1)->setAOI(eventX-prevX,prevY-eventY,0,0,true);

                }
            }
        }
		}//END OF if (!somethingPicked)
		else
		{
			//if something moved or something force a redraw!
			plateManager.setChanged();
		}
        prevX=eventX;
        prevY=eventY;

        return 1;
    }
    break;



    case FL_MOUSEWHEEL: {
        zoomSpeed=evt().isShift() ?0.01:0.1;
        //    	printf("evt().wheelDeltaY() %i\n",evt().wheelDeltaY());
        scale+=evt().wheelDeltaY() *zoomSpeed;
        //printf("scale=%f\n", scale);
        zooming=true;
        if ( scale<=0 )
            scale=0.001;
        if ( this->ID==LOADVP_ID ) {
            tp.scale+=evt().wheelDeltaY() *zoomSpeed;
            if ( tp.scale<0 )
                tp.scale=0.001;
        } else {
            if ( !evt().isAlt() ) {

                plateManager.zoomPlate(startQuad-1,evt().wheelDeltaY() *zoomSpeed);

                switch ( sett.framingMode ) {
                case FRAMINGSINGLE_ID:
                    q1.scale+=evt().wheelDeltaY() *zoomSpeed;
                    if ( q1.scale<0 )
                        q1.scale=0.001;
                    break;

                case FRAMINGDOUBLEVERT_ID:
                case FRAMINGDOUBLE_ID:
                    switch ( startQuad ) {
                    case 1:
                        q1.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q1.scale<0 )
                            q1.scale=0.001;
                        break;
                    case 2:
                        q2.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q2.scale<0 )
                            q2.scale=0.001;

                        break;
                    }
                    break;

                case FRAMINGQUAD_ID:
                    switch ( startQuad ) {
                    case 1:
                        q1.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q1.scale<0 )
                            q1.scale=0.001;

                        break;
                    case 2:
                        q2.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q2.scale<0 )
                            q2.scale=0.001;
                        break;
                    case 3:
                        q3.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q3.scale<0 )
                            q3.scale=0.001;

                        break;
                    case 4:
                        q4.scale+=evt().wheelDeltaY() *zoomSpeed;
                        if ( q4.scale<0 )
                            q4.scale=0.001;

                        break;
                    }
                    break;

                }
            } else {

                plateManager.zoomAllPlates(evt().wheelDeltaY() *zoomSpeed);

                /*q1.scale+=evt().wheelDeltaY() *zoomSpeed;
                if ( q1.scale<0 )
                	q1.scale=0.001;
                q2.scale+=evt().wheelDeltaY() *zoomSpeed;
                if ( q2.scale<0 )
                	q2.scale=0.001;
                q3.scale+=evt().wheelDeltaY() *zoomSpeed;
                if ( q3.scale<0 )
                	q3.scale=0.001;
                q4.scale+=evt().wheelDeltaY() *zoomSpeed;
                if ( q4.scale<0 )
                	q4.scale=0.001;*/

            }
        }
        //printf("scale: %f\n",scale);
        /*mw.q1Scale->value ( q1.scale );
        //             mw.q2Scale->value ( q2.scale );
        mw.q3Scale->value ( q3.scale );
        mw.q4Scale->value ( q4.scale );*/
        return 1;
    }
    break;

    case FL_PUSH: {
        int eventX=evt().mouseX(), eventY=evt().mouseY();
        // printf("click on X,Y: %i,%i",evt().mouseX(),evt().mouseY());
        dragging=true;

        //TODO: Create the flags DWORD
		
        pickManager.doPicking(GFC_PICK_EVENT_CLICK_DOWN, getGFCPICKFLAGSfromFLTK(), eventX, h()-eventY, 0,0);


        //swap_buffers(); //this is only here to debug picking so we can see the back buffer.
        if ( evt().isMouseButtonDown(jefe::ui::MouseButton::Left) ) {
            if (evt().isCtrl())
                fl_cursor(FL_CURSOR_NS);
            else
                fl_cursor ( FL_CURSOR_MOVE );
        }

        if ( evt().isMouseButtonDown(jefe::ui::MouseButton::Right)) { //show information on the track
            if (lw.loadWindow->visible()) {
                //NOTE: We do nothing here, this is because when loading, we use the 3rd mouse button to resize and move the AOI
                //printf("Load window visible??\n");
            } else {

                //show information on the track if "ctrl" is pressed
                if (evt().isCtrl()) {
                    fl_cursor ( FL_CURSOR_CROSS );
                    char tmpPopupText[2048];

                    GLubyte pixel[3];

                    //glGetIntegerv ( GL_VIEWPORT,viewport );
                    glReadBuffer(GL_FRONT);
                    glReadPixels ( evt().mouseX(),h()-evt().mouseY(),1,1,
                                   GL_RGB,GL_UNSIGNED_BYTE, ( void * ) pixel );
                    glReadBuffer(GL_BACK);

                    float luminance= 0.2126*pixel[0] + 0.7152*pixel[1] + 0.0722*pixel[2];
                    //printf("Luminance: %f\n",luminance);

                    sprintf ( tmpPopupText,"R: %03d (%.3f)\nG: %03d (%.3f)\nB: %03d (%.3f)\n\nY:%03i (%.3f)\n",pixel[0],pixel[0]/255.0,pixel[1],pixel[1]/255.0,pixel[2],pixel[2]/255.0, ( int ) luminance, ( int ) luminance/255.0 );

                    popup->text ( tmpPopupText );
                    popup->setColor ( pixel[0],pixel[1],pixel[2] );
                    popup->popup();
                }

                if (networkManager.getConnected()) {
										
                    Vec3D tmpVec;
                    int quad=getQuadFromMousePos(eventX,eventY)-1;
                    //calculate the cursor position in actual image space from the mouse position and the quad we are hovering over
                    tmpVec=plateManager.getCursorPositionIn2DSpace(eventX,h()-eventY,quad); //remember to invert the Y coordinate for openGL, also -1 since getQuad gets it in 1 based index, TODO: change the way getQuadFromMousePos returns junk,
                    //NOTE: the Z coord contains the scale!!!
                    //tell networkManager to send remote pointer (it will only send info when we actually are connected and the position is different from the last)
                    gfcNetPointerInfo info;
                    info.quadID=quad;
                    info.x=tmpVec.x;
                    info.y=tmpVec.y;
                    info.scale=tmpVec.z;
                    networkManager.sendPointerInfoMessage(info);
                }


            }
        }


        Fl::focus ( this );

        prevX=evt().mouseX();
        prevY=evt().mouseY();
        startQuad=this->getQuadFromMousePos(evt().mouseX(),evt().mouseY());
		
		plateManager.setActiveQuad(startQuad-1);
	
        if ( fxControlWindow1.theWindow->shown() ) {
            fxControlWindow1.scheduleUpdateWindow ( startQuad-1 );
            //Fl::focus ( this );
            Fl::check();
        }




        return 1;
    }
    break;


    case FL_RELEASE: {
        int eventX=evt().mouseX(), eventY=evt().mouseY();
        pickManager.doPicking(GFC_PICK_EVENT_CLICK_UP, getGFCPICKFLAGSfromFLTK(), eventX, h()-eventY, 0,0);
        popup->hide();
        fl_cursor ( FL_CURSOR_DEFAULT );
        dragging=false;

        transformingDragging=false;
        return 1;
    }
    break;

    case FL_PASTE: {
        //*****HANDLE DRAG AND DROP********//
        if (networkManager.gChatMode==0) { //this is probably a drag and drop event, so we load the sequence
            std::string pastedText=evt().currentText().c_str();
            std::cout<<"pasted text: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;

            //printf("\nDropped %s into track\n",RemoveNewLine(GetFilenameNoFilePrefix(evt().currentText().c_str())).c_str());
            int quadFromMouse=getQuadFromMousePos(pastedXPos,pastedYPos)-1;
            int trackID=plateManager.getTrackOnPlate(quadFromMouse);
            //printf("DnD: quad:%i, trackID: %i\n",quadFromMouse,trackID);

            if (trackID>-1) {
                std::string cleanText=GetFilenameNoFilePrefix(RemoveNewLine(pastedText));
                if (!cleanText.empty() && cleanText!=" ") {

                    //check if it is a session
                    if (lowerCase(GetExtension(cleanText))=="jcs") {
                    	printf("Dragged a session\n");
                    	sessionManager.loadSession(cleanText);
                    } else {
                        //check if it is a playlist
                        if (lowerCase(GetExtension(cleanText))=="jpl") {
                            printf("Dragged a playlist\n");
                            playlistManager.loadPlaylist(cleanText);

                            if (!plw.theWindow->visible()) {
                                plw.theWindow->show();
                            }
                            plw.updateWindow();
                        } else {
                            //check if it is an FX Stack
                            if (lowerCase(GetExtension(cleanText))=="fxs") {
                                printf("Dragged an FX Stack\n");
                                if (quadFromMouse>-1) {
                                    plateManager.loadStackFromFile(quadFromMouse,cleanText);
                                    fxControlWindow1.scheduleUpdateWindow(quadFromMouse);
                                }

                            } else {
								if(lowerCase(GetExtension(cleanText))=="jpl")
								{
									
										printf("Dropped playlist! %s\n",cleanText.c_str());
										playlistManager.loadPlaylist(cleanText);
									
								}
								else{
								//otherwise asume it is a sequence.
								
								cleanText=getFirstSequenceInDirectory(cleanText); //if it's a directory we get the first file, otherwise we return the same name.

                                trackManager.getSequence(trackID)->myGUI->setFilename(cleanText);
                                if (evt().isShift() || evt().isShift()) {
                                    trackManager.getSequence(trackID)->myGUI->setScale("50");
                                } else {
                                    trackManager.getSequence(trackID)->myGUI->setScale("100");
                                }

                                trackManager.loadPreviewFrame(trackID);

                                if (!lw.loadWindow->visible()) {
                                    //printf("we want to start!\n");
                                    trackManager.startLoadingSequence(trackID);
                                }
				}

                            }
                        }
                    }
                }





            }
        } else if (networkManager.gChatMode==1) {	//TODO: paste into chat!
            printf("Pasted into chat!: %s\n",evt().currentText().c_str());
        }


        break;

        //case FL_SHORTCUT:
        case FL_KEYDOWN: {

            if ( networkManager.gChatMode==0) { //we only handle keyboard events in the viewport.when in chat mode.


                plateManager.setChanged();

                switch (static_cast<int>(evt().currentKey())) {
                               
                
                case FL_F+2:
                if(!evt().isShift() && !evt().isCtrl()){
                    fxControlWindow1.theWindow->show();
                    return 1;}
                    break;

                case FL_F+3:
                if(!evt().isShift() && !evt().isCtrl()){
                    fxw.fxWindow->show();
                    return 1;}
                    break;

                case FL_F+4:
                if(!evt().isShift() && !evt().isCtrl()){
                    lutw.lutWindow->show();

                    return 1;
                    }
                    break;

                case FL_F+5:
                if(!evt().isShift() && !evt().isCtrl()){
                    rmw.remoteWindow->show();
                    
                    return 1;
                    }
                    break;

                case FL_F+6:
                    rw.renderWindow->show();
                    return 1;
                    break;

                case 32: //space bar
                    playbackManager.pause();
                    return 1;

                case FL_Left:
                    playbackManager.oneFrameRev();
                    Fl::focus(mw.vp);
                    return 1;
                    break;

                case FL_Right:
                    playbackManager.oneFrameFwd();
                    Fl::focus(mw.vp);
                    return 1;
                    break;

                case FL_Up: {
					
					if (evt().isKeyDown(static_cast<jefe::ui::Key>('L')))
					{ //if L is pressed, change LUTs
						plateManager.scrollLUT(plateManager.getActiveQuad(),-1);
					} 
					else
					{
						int currentTrack=plateManager.getTrackOnPlate(startQuad-1);
						currentTrack--;
						currentTrack=currentTrack<0?3:currentTrack;
						plateManager.setTrackOnPlate(startQuad-1,currentTrack);	
					}
					
                    
                    return 1;
                }
                break;

                case FL_Down: {
					if (evt().isKeyDown(static_cast<jefe::ui::Key>('L')))
					{ //if L is pressed, change LUTs
						plateManager.scrollLUT(plateManager.getActiveQuad(),1);
					} 
					else
					{
                    int currentTrack=plateManager.getTrackOnPlate(startQuad-1);
                    plateManager.setTrackOnPlate(startQuad-1,(currentTrack+1)%4);
					}
                    return 1;
                }
                break;
                }

                //printf("Returning 0 from no chat mode event %c (%i)\n",static_cast<int>(evt().currentKey()),static_cast<int>(evt().currentKey()));
                return 0;
            }
            plateManager.setChanged();
            //handle special ctrl keyboard events, otherwise return 0 to pass down the line in fltk (to open the prefs window with ctrl-p for example)
            if (evt().isCtrl()) {
                switch ( static_cast<int>(evt().currentKey()) ) {
				
		                case 'v': {
                    printf("Paste from clipboard?!\n");
                    Fl::paste(*((Fl_Widget*)this),1); //this will eventually call this handle with a FL_PASTE event, which we use to paste into chat, if we are in chat mode...
                }
                return 1;
                break;

                case 'c':

                    return 1;
                    break;

                case 'l':
                    lw.loadWindow->show();
                    break;

                case 'p':
                    pw.preferencesWindow->show();
                    break;

                default: { //when we don't handle a ctrl+key event simply pass along to fltk by returning 0
                    //printf("Returning 0 from ctrl+key event\n");
                    return 0;
                }
                }

                return 0;

            }

            switch ( static_cast<int>(evt().currentKey()) ) {



            case FL_Enter:

                //printf ( "FL_ENTER PRESSED\n" );
                //if ctrl+enter wass pressed, dont leave chat mode
                networkManager.gChatMode=evt().isCtrl() ?0:1;
                networkManager.sendChatMessage();
                networkManager.gChatTextString.clear();
                networkManager.chatPosOffset=0;

                return 1;
                break;

            case FL_BackSpace:
                if ( !networkManager.gChatTextString.empty() ) {
                    networkManager.gChatTextString.erase( networkManager.chatPosOffset-1,1);
                    networkManager.chatPosOffset--;
                }
                return 1;
                break;

            case FL_Delete:
                if ( !networkManager.gChatTextString.empty() && networkManager.chatPosOffset<networkManager.gChatTextString.size()) {
                    networkManager.gChatTextString.erase( networkManager.chatPosOffset,1);
                }
                return 1;
                break;

            case FL_Escape:
                networkManager.gChatMode=0;
                networkManager.chatFadeCounter= networkManager.chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
                return 1;
                break;

            case FL_Up:
                networkManager.chatLineOffset++;
                return 1;
                break;


            case FL_Down:
                if ( networkManager.chatLineOffset>0 )
                    networkManager.chatLineOffset--;
                return 1;
                break;

            case FL_Left:
                if ( networkManager.chatPosOffset>0 )
                    networkManager.chatPosOffset--;
                return 1;
                break;

            case FL_Right:
                if ( networkManager.chatPosOffset<networkManager.gChatTextString.size() )
                    networkManager.chatPosOffset++;
                return 1;
                break;
            }

            {

                if ( networkManager.gChatTextString.size() >254 ) {
                    printf ( "\a" );
                    return 1;
                }

                if ( strcmp ( "@",evt().currentText().c_str() ) ==0 ) {
                    //at shoud be handled differently due to fltks use of it for symbols. Append two @@ to cancell
                    networkManager.gChatTextString.insert(networkManager.chatPosOffset,"@@");
                    networkManager.chatPosOffset+=2;
                    //networkManager.gChatTextString+="@@";

                    return 1;
                }
                int del;
                Fl::compose(del);

                if ( del && !networkManager.gChatTextString.empty() ) {
                    networkManager.gChatTextString.erase ( networkManager.chatPosOffset-del,del );
                    networkManager.chatPosOffset-=del;
                }


                /*printf("\nchatPosOffset=%i\n",networkManager.chatPosOffset);
                printf("eventText=%s\n",evt().currentText().c_str());
                printf("event_lenght=%i\n",static_cast<int>(evt().currentText().size()));*/

                if (static_cast<int>(evt().currentText().size())>0 && evt().currentText().c_str()!=NULL)
                    networkManager.gChatTextString.insert(networkManager.chatPosOffset,evt().currentText().c_str());

                networkManager.chatPosOffset+=static_cast<int>(evt().currentText().size());
                if (networkManager.chatPosOffset>=networkManager.gChatTextString.size())
                    networkManager.chatPosOffset=networkManager.gChatTextString.size();

                return 1;
            }
        }

    }
    }
    return 0;
}
void GlViewport::reLayout ( void ) {
    //	layout();
}

std::string GlViewport::updateTimecode ( void ) {
    int hours, minutes, seconds, frms;
    char timeCode[15]="";

    hours=0;
    minutes=0;
    seconds=0;
    if ( mw.vp->targetFPS!=0 ) {
        frms=(currentFrame)%targetFPS;
        seconds= ( currentFrame/ ( targetFPS ) ) %60;
        minutes= ( currentFrame/ ( targetFPS*60 ) ) %60;
        hours= ( currentFrame/ ( targetFPS*3600 ) );
    }
    sprintf ( timeCode,"%02i:%02i:%02i:%02i",hours, minutes, seconds, frms );
    mw.timeCodeOutput->value ( timeCode );

    return timeCode;
}

int GlViewport::getMaxTrackLenght ( void ) {
    //TODO: getMaxTrackLenght should not always return 100!!
    int max=100;
    /*
        if(trackA.numFrames>max)
            max=trackA.numFrames;
     
        if(trackB.numFrames>max)
            max=trackB.numFrames;
     
        if(trackC.numFrames>max)
            max=trackC.numFrames;
     
        if(trackD.numFrames>max)
            max=trackD.numFrames;*/


    /*if ( trackA.files.size() >max )
    	max=trackA.files.size();

    if ( trackB.files.size() >max )
    	max=trackB.files.size();

    if ( trackC.files.size() >max )
    	max=trackC.files.size();

    if ( trackD.files.size() >max )
    	max=trackD.files.size();*/

    return max;
}

int GlViewport::getQuadFromMousePos(int x, int y) {
    switch ( plateManager.getFramingMode()) {
    case FRAMINGSINGLE_ID:
        return 1;
        break;
    case FRAMINGDOUBLE_ID:
        if ( x <w() /2.0 )
            return 1;
        else
            return 2;
        break;

    case FRAMINGDOUBLEVERT_ID:
        if ( y <h() /2.0 )
            return 1;
        else
            return 2;
        break;

    case FRAMINGQUAD_ID:
        if ( x <w() /2.0 ) { //left side
            if ( y < h()/2.0 ) { //bottom
                return 1;
            } else { //top
                return 3;
            }

        } else { //right side
            if ( y <h() /2.0 ) { //
                return 2;
            } else {
                return 4;
            }

        }
        break;

        break;
    }
}
