#include "glew.h"
#include "gfcPlate.h"
#include "gfcSequence.h"
#include "mainWindow.h"
#include "loadWindow.h"
#include "lutWindow.h"
#include "FL/fl_ask.H"
#include "gfcfx.h"
#include <string>
#include "platefxparams.h"
#include "renderWindow.h"
//#include "network.h"
#include "gfcplatedrawparams.h"

#include "gfcStructures.h"

#include <iostream>
#include <sstream>
#include <iomanip>

#include "gfcimagesaver.h"

#include "gfchistogram.h"



extern MainWindow mw;
extern LoadWindow lw;
extern LutWindow lutw;
extern RenderWindow rw;
//extern std::vector<CubeLUT> lutArray;
extern bool npotTextures;
extern float gFPS;
extern GLint gFilteringModeMin;
extern GLint gFilteringModeMag;
extern std::vector<gfcFX> fxArray;
extern std::vector<gfcFX> fxApplied[4];
extern std::vector<int> fxArrayActiveCount; //how many fx are active for each quadrant.
extern int numberOfActiveEffects[4];
extern float timeStep;

extern bool gConnected;
extern std::vector<std::string> chatLog;
extern std::string gChatTextString;
extern int gChatMode;
extern float chatFadeCounter;
extern int chatLineOffset;

extern std::map<std::string,gfcNetRemotePointerInfo> nickNamePointerMap;

extern bool dragging; //if the mouse is dragging on screen, used to capture and store the mouse position for later sending it to server

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#define SHADOWOFFSET 1

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

//#ifdef WIN32
//#include <GL/wglext.h>
//#include <gl/glext.h>
//#else
//#include <GL/glext.h>
//#endif




void printViewport() {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT,vp);
    printf("GLViewport: %i,%i,%i,%i\n",vp[0],vp[1],vp[2],vp[3]);
}

void printModelView() {
    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX , mv);
    printf("ModelView:");
    for (int i=0; i<16;i++)
        printf("%f ",mv[i]);

    printf("\n\n");
}

gfcPlate::gfcPlate ( void )
        : //textureID(0)
        track ( 0 )
        , scale ( 1 )
        , tX ( 0 )
        , tY ( 0 )
        , rX ( 0 )
        , rY ( 0 )
        , rZ ( 0 )
        , choice ( 0 )
        , active ( false )
        , showText ( true )
        ,aspectChoice ( NULL )
        ,textMode(1)
        ,aspect ( -1 )
        ,cropMode ( GFCCROPCENTER )
        ,cropOn ( false )
        ,rMask ( 1 )
        ,bMask ( 1 )
        ,gMask ( 1 )
        ,aMask ( 0 )
        ,forRender ( 0 ),
        flip(false),
        flop(false),
        textDisplaySize(12.0),
        textDisplayColor(1.0),
        textDisplayOpacity(1.0),
        forceSingleBufferedFX(false)

{

    showHelp=false;
    
    histogramQuality=16;
    
    histogramWindow.visible(false);
//    histogramWindow.windowSize=256;
    
    /*if ( sett.fbo ) {
        glGenFramebuffersEXT ( 1, &fbo );
    }*/

    remotePointerSize=5;
    remotePointerFontSize=14;


    renderModeSelection=0;
	
	fbov[0]=fbov[1]=fbov[2]=0;
	fboTexturev[0]=fboTexturev[1]=fboTexturev[2]=0;


}


gfcPlate::~gfcPlate ( void ) {}

void gfcPlate::capturePointerCoords() {	//TEST CAPTURING THE GL COORDINATES FROM THE WINDOW COORDINATES
    static GLdouble projection[16],modelView[16];
    static GLint viewport[4];
    static GLdouble posX=0,posY=0,posZ=0;

    if (dragging) {

        glGetIntegerv( GL_VIEWPORT,viewport );
        glGetDoublev( GL_MODELVIEW_MATRIX,modelView );
        glGetDoublev( GL_PROJECTION_MATRIX,projection );

        gluUnProject ( mw.vp->prevX, mw.vp->h()-mw.vp->prevY, -1, modelView, projection, viewport, &posX, &posY, &posZ );
        prevPointerX=posX;
        prevPointerY=posY;

        //printf ( "%i: pos ( %i,%i ) : %f,%f,%f\n",this->quadID,mw.vp->prevX,mw.vp->prevY,posX,posY,posZ );
    }


}

std::string gfcPlate::getInfoLog ( GLhandleARB obj )
{
	int infologLength = 0;
	int charsWritten  = 0;
	char *infoLog;
	std::string returnValue;
	glGetObjectParameterivARB ( obj, GL_OBJECT_INFO_LOG_LENGTH_ARB,
		( GLint* ) &infologLength );

	if ( infologLength > 0 )
	{
		infoLog = ( char * ) malloc ( infologLength );
		glGetInfoLogARB ( obj, infologLength, ( GLint* ) &charsWritten, infoLog );
		returnValue = infoLog;
		//printf("%s\n",infoLog);
		free ( infoLog );
		return returnValue;
	}
}

void gfcPlate::startSuperShader(){
	//return;
	this->recompileSuperShader();
	//return;
	//if (usingLUT || usingGammaExp || usingBCS || usingRGBAMasks)
	{
		if(ssProgramCreated && useShader)
		{
			//bind program
			glGetError();
			glUseProgramObjectARB ( ssProgram );
			glPrintError();	
			//pass attributes

			if(usingLUT){
				//lut
				glEnable ( GL_TEXTURE_1D );
				//glEnable ( GL_TEXTURE_3D );
				glActiveTexture ( GL_TEXTURE0_ARB+4 ); //the LUTs go in texture unit 1
				//do a switch to figure out from what sequence we will assign the texture, or if we use the previousTexID
				//printf("binding 3D cube: %i to variable %s in unit GL_TEXTURE0+%i\n",lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture3D,groupsIter->second.widgets[*widgetIter].varName.c_str(),CubeUnitCounter);
				glBindTexture ( this->lutType==CubeLUT::JEFECHECK1D?GL_TEXTURE_1D:GL_TEXTURE_3D,this->lutID);
				
				GLuint location=glGetUniformLocationARB(ssProgram,"LUT");
				if(location!=-1){
					glUniform1iARB(location, 4); //the parameter we assign to the uniform variable is the texture unit this texture was assigned to.
					//printf("Assigning lut1D: %i to %s on texUnit: %i on location: %i\n",lutID,this->lutType==CubeLUT::JEFECHECK1D?"GL_TEXTURE_1D":"GL_TEXTURE_3D",GL_TEXTURE0_ARB+1,location);
				}
				glActiveTexture ( GL_TEXTURE0_ARB );

				//also pass the lutSize to use in 3d lut calculations
				{
					location=glGetUniformLocationARB(ssProgram,"lutSize");
					if(location!=-1){
						glUniform1fARB(location, (float)this->lutSize); 
					}
				}
			}
			
			
			//THE ACTUAL IMAGE
			{
				glActiveTexture(GL_TEXTURE0_ARB);
				switch(textureTypeForSuperShader)
				{
				case GFC_S3TCDX1:
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D,textureIDforSuperShader);
					break;
				default:
					glEnable(GL_TEXTURE_RECTANGLE_ARB);
					glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textureIDforSuperShader);
					break;
				}
				
				GLuint location=glGetUniformLocationARB (ssProgram,"image");
				if(location!=-1){
					//printf("Assigning texture unit 0 to location: %i\n",location);
					glUniform1iARB ( location, 0); //the parameter we assign to the uniform variable is the texture unit this texture was assigned to.	
				}
				else
				{
					//printf("Could not assign texture unit to wrong location %i\n",location);
				}
				
			}
			
			if(usingRGBAMasks){
				//RGB
				GLuint location=glGetUniformLocationARB (ssProgram,"R");
				if(location!=-1){
					glUniform1fARB (location,(GLfloat)rMask);
					//printf("ASsigning R: %i on location: %ui\n",rMask,location);
				}
				else
				{
					//printf("R got incorrect address: %i\n");
				}

				location=glGetUniformLocationARB (ssProgram,"G");
				if(location!=-1){
					glUniform1fARB (location,this->gMask);
					//printf("ASsigning G: %i on location: %i\n",gMask,location);
				}
				else
				{
					//printf("G got incorrect address: %i\n");
				}

				location=glGetUniformLocationARB (ssProgram,"B");
				if(location!=-1){
					glUniform1fARB (location,this->bMask);
					//printf("ASsigning B: %i on location: %i\n",bMask,location);
				}
				else
				{
					//printf("B got incorrect address: %i\n");
				}

				location=glGetUniformLocationARB (ssProgram,"A");
				if(location!=-1){
					glUniform1fARB (location, this->aMask);
					//printf("Assigning A: %i on location: %i\n",aMask,location);
				}
				else
				{
					//printf("A got incorrect address: %i\n");
				}

			}


			if(usingGammaExp){
				//gamma
				GLuint location=glGetUniformLocationARB (ssProgram,"Gamma");
				if(location!=-1){
					glUniform1fARB (location,this->gamma);
					//printf("ASsigning gamma: %f on location: %i\n",gamma,location);
				}
				else
				{
					//printf("Gamma got incorrect address: %i\n");
				}

					//exposure
					location=glGetUniformLocationARB ( ssProgram,"Exposure");
					if(location!=-1)
						glUniform1fARB (location, this->exposure);
			}

			if(usingBCS){
				//brightness
				GLuint location=glGetUniformLocationARB ( ssProgram,"Brightness");
				if(location!=-1)
					glUniform1fARB (location, this->brightness);
			
				//contrast
				location=glGetUniformLocationARB ( ssProgram,"Contrast");
				if(location!=-1)
					glUniform1fARB (location, this->contrast);
			
				//saturation
				location=glGetUniformLocationARB ( ssProgram,"Saturation");
				if(location!=-1)
					glUniform1fARB (location, this->saturation);
			}
			

		}
	}
}

void gfcPlate::stopSuperShader()
{
	glUseProgramObjectARB ( 0 );
}

void gfcPlate::buildShader(int useLut,int useGammaExp, int useBCS, int useRGBAMask,int textureType)
{
	ssFramgentCompiled=0;
	
	std::string vertexSrc="void main(){\n\
					    gl_TexCoord[0] = gl_MultiTexCoord0;\n\
						gl_TexCoord[1] = gl_MultiTexCoord1;\n\
						gl_TexCoord[2] = gl_MultiTexCoord2;\n\
						gl_TexCoord[3] = gl_MultiTexCoord3;\n\
	                    gl_Position = ftransform();\n\
					   }" ;
	
	ssVertexSource=vertexSrc;

	std::string fragmentSrc="#extension GL_ARB_texture_rectangle : require\n\
		uniform float Exposure;\n\
		uniform float Gamma;\n\
		uniform float Brightness;\n\
		uniform float Saturation;\n\
	    uniform float Contrast;\n\
		const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);\n\
		uniform float R;\n\
		uniform float G;\n\
		uniform float B;\n\
		uniform float A;\n\
		uniform float lutSize;\n ";
	
	//different image formats require different samplers
	std::string samplingFunctionText="";
	switch(textureType)
	{
	case GFC_S3TCDX1:
		fragmentSrc+="uniform sampler2D image;\n";
		samplingFunctionText="texture2D";
		break;
	default:
		fragmentSrc+="uniform sampler2DRect image;\n";
		samplingFunctionText="texture2DRect";
	    break;
	}
	
	//different LUT types require different samplers and sampling methods
	if (lutType==CubeLUT::JEFECHECK1D && useLut)
	{
		fragmentSrc+="uniform sampler1D LUT;\n";
		fragmentSrc+=std::string("\
					void main(){\n\
					vec4 cubeCoords=")+samplingFunctionText+std::string("(image,gl_TexCoord[0].st);	\n\
					vec4 theColor=vec4(texture1D(LUT,cubeCoords.r).r,texture1D(LUT,cubeCoords.g).g,texture1D(LUT,cubeCoords.b).b,cubeCoords.a);\n");
		
		currentLUTType=lutType;
	}
	else
	{
		if (useLut && (lutType==CubeLUT::BASELIGHT3DCUBE || CubeLUT::IMAGELUT2D))
		{
			fragmentSrc+="uniform sampler3D LUT;\n";
			fragmentSrc+=std::string("\
									 void main(){\n\
									 vec4 cubeCoords=")+samplingFunctionText+std::string("(image,gl_TexCoord[0].st)*((lutSize-1.0)/lutSize) + 1.0 /(lutSize*2.0); \n\
									 vec4 theColor=vec4(vec3(texture3D(LUT,cubeCoords.rgb).rgb),cubeCoords.a); \n");
			/*fragmentSrc+=std::string("\
						 void main(){\n\
						 vec4 cubeCoords=")+samplingFunctionText+std::string("(image,gl_TexCoord[0].st)*(15.0/16.0) + 1.0 /32.0; \n\
						 vec4 theColor=vec4(vec3(texture3D(LUT,cubeCoords.rgb).rgb),cubeCoords.a); \n");*/
			currentLUTType=lutType;
		}
		else
		{
			//just extract the texture, no lut here
			fragmentSrc+=std::string("\
						 void main(){\n\
						 vec4 theColor=")+samplingFunctionText+std::string("(image,gl_TexCoord[0].st);\n");
		}
	}
		
	

	if (useGammaExp)
	{
		fragmentSrc+="\
			theColor=theColor*pow(vec4(2.0),vec4((Exposure))); //adjust exposure \n\
			theColor =pow(theColor,vec4((1.0/Gamma))); //apply video gammma \n";
	}

	if (useBCS)
	{
		fragmentSrc+="\
		float originalAlpha=theColor.a;\nfloat AvgLuminance=0.5;\n\
		theColor*=Brightness;\n\
		vec4 tmpIntensity=vec4(dot(theColor.rgb,lumCoeff));\n\
		vec4 satResult= mix(theColor,tmpIntensity,1.0-Saturation);\n\
		vec3 avgLuminanceVec=vec3(AvgLuminance);\n\
		vec3 contResult=mix(avgLuminanceVec,satResult.rgb,Contrast);\n\
		theColor = vec4(contResult,originalAlpha);\n";
	}

	if (useRGBAMask)
	{
		fragmentSrc+="\n\
					 float gs=R*theColor.r + G*theColor.g + B*theColor.b + A*theColor.a; \
					 theColor=vec4(gs,gs,gs, theColor.a);\n	";
	}

	fragmentSrc+="\ngl_FragColor=theColor;\n\
				 }\n";

	//fragmentSrc="void main(){ gl_FragColor=vec4(gl_TexCoord[0].s/1000.0,gl_TexCoord[0].t/1000.0,0.0,1.0);}";
	
	ssFragmentSource=fragmentSrc;
	//printf("This is the fragment shader:\n %s\n",fragmentSrc.c_str());
	

	//compile vertex shader;
	if (!ssVertexCompiled)
	{
		//char *src=new char[ssVertexSource.size()];
		char src[4096]="";
		strcpy(src,ssVertexSource.c_str());
		const char *vv=src;
		//printf("\n\n*******THIS IS src:\n%s\n\n***********\n",src);
		//create object, load source, compile
		glDeleteObjectARB(ssVertexShader);
		ssVertexShader=glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
		glShaderSource(ssVertexShader,1,&vv,NULL);
		glCompileShaderARB(ssVertexShader);
		
		std::string infoLog;
		infoLog = getInfoLog ( ssVertexShader );
		if ( infoLog.size() >0 )
		{
			printf ("Vertex Shader compilation error!:\n%s\n",infoLog.c_str() );
			//errorWhileLoading=true;
			//return 1;
		}
		else
		{
			//printf("Vertex Compiled..OK!\n");
		}
		ssVertexCompiled=1;
		//vv=NULL;
		//delete [] src;
		ssProgramCreated=0;
	}

	//compile fragment shader
	if (!ssFramgentCompiled /*&& false*/)
	{
		glDeleteObjectARB(ssFramgmentShader);
		ssFramgmentShader=glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
		//printf("ssFragmentSource.size()=%i\n",ssFragmentSource.size());
		//char *src=new char[ssFragmentSource.size()];
		char src[4096]="";
		strcpy(src,ssFragmentSource.c_str());
		const char * vv=src;

		glShaderSource(ssFramgmentShader,1,&vv,NULL);
		//glShaderSource(ssFramgmentShader,1,&src,NULL);
		glCompileShaderARB(ssFramgmentShader);
		//delete [] src;
		std::string infoLog;
		infoLog = getInfoLog ( ssFramgmentShader );
		if ( infoLog.size() >0 )
		{
			printf ( "Fragment Shader compilation error!:\n%s\n",infoLog.c_str() );
			printf("\nThis is the fragment shader:\n*************\n%s\n*************\n",fragmentSrc.c_str());
			//errorWhileLoading=true;
			//return 1;
		}
		else
		{
			//printf("Fragment Compiled..OK!:%s\n",infoLog.c_str());
		}
		ssFramgentCompiled=1;
		ssProgramCreated=0;
	}

	if (!ssProgramCreated)
	{
		glDeleteObjectARB(ssProgram);
		ssProgram=glCreateProgramObjectARB();
		glAttachObjectARB(ssProgram,ssVertexShader);
		glAttachObjectARB(ssProgram,ssFramgmentShader);

		glLinkProgram(ssProgram);
		
		std::string infoLog;
		infoLog = getInfoLog ( ssProgram );
		if ( ( strstr ( infoLog.c_str(),"error" ) !=0 ) )
		{
			printf ( "Shader Program Linking error!:\n%s\n",infoLog.c_str() );
			
		}
		else
		{
			//printf ( "Shader Program Linker OK!:%s\n",infoLog.c_str());
			ssProgramCreated=1;
		}
		

	}

	usingLUT=useLut;
	usingGammaExp=useGammaExp;
	usingBCS=useBCS;
	usingRGBAMasks=useRGBAMask;
	usingTextureType=textureType;
}

void gfcPlate::recompileSuperShader()
{
	//this method decides if we need to build the shader and also tells us if we should use the shader (maybe it's built but we don't want to use it)

	int useLUT=(lutID>=0);
	int differentTypeOfLUT=(useLUT && this->currentLUTType!=this->lutType);
	int useGammaExp=(gamma!=1.0 || exposure!=0);
	int useBCS=(brightness!=1.0 || contrast!=1.0 || saturation!=1.0 );
	int useRGBAMasks=(!rMask || !gMask || !bMask || aMask);
	int textureType = textureTypeForSuperShader;

		
	if (useLUT || useGammaExp || useBCS || useRGBAMasks || usingTextureType!=textureType)
	{
		useShader=true;
	}
	else
	{
		useShader=false;
	}

	//if we need to use something different than the shader that is already built, then rebuild and remember what we are using now
	if (useShader && ((useLUT!=usingLUT) || (useGammaExp!=usingGammaExp) || useBCS!=usingBCS || differentTypeOfLUT || useRGBAMasks!=usingRGBAMasks || usingTextureType!=textureType) )
	{
		//printf("Rebuilding new shader: useLUT:%i (%i), useGammaExp:%i (%i), useBCS:%i (%i) textureType:%i(%i)\n",useLUT,usingLUT, useGammaExp, usingGammaExp, useBCS, usingGammaExp,textureType,usingTextureType);
		
		buildShader(useLUT,useGammaExp,useBCS,useRGBAMasks,textureType);
	}
	
	
	
	
	
	
}

bool gfcPlate::createFBO() {

    ////////////////////DUAL FBO APROACH
//     if ( sett.fbo ) {
//
//             glDeleteFramebuffersEXT ( 2,fbov );
//             glDeleteTextures ( 2,fboTexturev );
//
//         glEnable ( GL_TEXTURE_RECTANGLE_ARB );
//
//         glGenTextures ( 2, fboTexturev );
//         glGenFramebuffersEXT ( 2, fbov );
//
// 	for(int i=0;i<2;i++)
//         {
//         glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbov[i] );
//         glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, fboTexturev[i] );
//
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
//
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST );
//
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE_EXT );
//         glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE_EXT );
//
//         glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, sett.fp16?GL_RGBA16F_ARB:GL_RGBA,  fboVP.w, fboVP.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
//         glFramebufferTexture2DEXT ( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, fboTexturev[i], 0 );
//
//         //glCheck();
//         //printf("Q%i:\n *generated FBO id:%i\n *RTT Info: (%i,%i),%ix%i\n *rect: (%i,%i),%ix%i\n\n",quadID,fbo,fboVP.x,fboVP.y,fboVP.w,fboVP.h,rect.x,rect.y,rect.w,rect.h);
//         //glPrintError();
//         glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, 0 );
//         glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, 0 );
//         }
//         return 1;
//     } else {
//         return 0;
//     }

    //////////////SINGLE FBO, DUAL TEXTURE APPROACH
    if ( sett.fbo ) {

        bool success=false;
        multiFormatFBOSupported=true;
        int FBOerrors=0;

        int count=3;
        if (forceSingleBufferedFX) {
            count = 2;
        }

        //start assuming that we have newer hardware and can use three total buffers (or two for single buffered).
        while (!success) {
            fboTexturevCount=count;
			//if (fbov)
			//{
			printf("Deleting FrameBufferExt: %i\n",fbov[0]);
				glDeleteFramebuffersEXT ( 1,fbov );
				fbov[0]=fbov[1]=fbov[2]=0;
			printf("Deleting %i fboTexturev: %i %i %i\n",3, fboTexturev[0],fboTexturev[1],fboTexturev[2]);
				glDeleteTextures ( 3,fboTexturev );
				fboTexturev[0]=fboTexturev[1]=fboTexturev[2]=0;
			//}
			
            

            glEnable ( GL_TEXTURE_RECTANGLE_ARB );

            glGenTextures ( 3, fboTexturev );
            glGenFramebuffersEXT ( 1, fbov );

            glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbov[0] );
            for (int i=0;i<count;i++) {

                glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, fboTexturev[i] );

                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR );

                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST );

                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE_EXT );
                glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE_EXT );

                if (i==count-1 && multiFormatFBOSupported) {
                    //this is the special case where we create the 8 bit texture to use later in the histogram
                    glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,  fboVP.w, fboVP.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
                    //glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, sett.fp16?GL_RGBA16F_ARB:GL_RGBA,  fboVP.w, fboVP.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
                    //printf("8 bit texture name:  %i  index: %i\n",fboTexturev[i],i);
                    fbo8bitTexture=fboTexturev[i];
                } else {
                    glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, sett.fp16?GL_RGBA16F_ARB:GL_RGBA,  fboVP.w, fboVP.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
                }
                glPrintError();
                glFramebufferTexture2DEXT ( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT+i, GL_TEXTURE_RECTANGLE_ARB, fboTexturev[i], 0 );
                glPrintError();


                glCheckFBO(FBOerrors);
                if (FBOerrors) { //this means multiFormatFBO are not supported
                    multiFormatFBOSupported=false;
                    count--;
                }
				else
				{
					printf("No FBO Errors with count = %i\n",count);
				}
                //printf("Q%i:\n *generated FBO id:%i\n *RTT Info: (%i,%i),%ix%i\n *rect: (%i,%i),%ix%i\n\n",quadID,fbo,fboVP.x,fboVP.y,fboVP.w,fboVP.h,rect.x,rect.y,rect.w,rect.h);

                glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, 0 );
            }

            if (FBOerrors==0) {
                success=true;
            } else {
                FBOerrors=0;
            }
        }
        glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, 0 );

        /*TODO: Also create an 8 bit fbo so we can render the result there and use that
        	texture for the histogram and other operations that require readback.*/



        if (!multiFormatFBOSupported) {
            printf("Multiformat FBOs NOT supported\n");
            //this means we need a separate FBO for the 8 bit texture because we can't attach it to the FBO we already use.
            glDeleteFramebuffersEXT ( 1,&fbo8bit );
            glDeleteTextures (count,&fbo8bitTexture);

            glEnable (GL_TEXTURE_RECTANGLE_ARB);

            glGenTextures (count, &fbo8bitTexture);
            glGenFramebuffersEXT (1, &fbo8bit);


            glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbo8bit );
            glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, fbo8bitTexture );

            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR );

            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST );

            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE_EXT );
            glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE_EXT );

            glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,  fboVP.w, fboVP.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
            glFramebufferTexture2DEXT ( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT+fboTexturevCount, GL_TEXTURE_RECTANGLE_ARB, fbo8bitTexture, 0 );
            FBOerrors=0;
            glCheckFBO(FBOerrors);
            glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, 0 );
            printf("Created extra FBO for the 8 bit target\n");
        } else {
            printf("Multiformat FBOs supported\n");
        }

        //END OF CREATING AN 8BIT FBO


        glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, 0 );
        glBindTexture ( GL_TEXTURE_RECTANGLE_ARB, 0 );
        return 1;
    } else {
        return 0;
    }
}

void gfcPlate::updateAnimations() {
    //this function handles all the fading and animation in the remote pointers and other animated stuff,
    //we separete it from the drawing function
    //so we can have a smooth animation but still only draw when something has changed.
    pointerStorage.updateFaders();
    updateRot(playbackManager.getTimestep(),flip,flop);
}

void gfcPlate::drawRemotePointers() {
    if (!pointerStorage.empty()) {
        //plateManager.setChanged();
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        //for each nickname draw the pointer
        std::map<std::string, std::deque<gfcNetRemotePointerInfo> >::iterator nickIter=pointerStorage.pointerMap.begin(), end=pointerStorage.pointerMap.end();

        std::deque<gfcNetRemotePointerInfo>::iterator pIter, pEnd;

        int counter=0;
        float fader=1;

        
        //printf("remotePointerColor=%i (%i, %i, %i)\n",remotePointerColor, red, green, blue);
        for (nickIter;nickIter!=end;nickIter++) {
            glPushMatrix();
            counter=0;
            pIter=nickIter->second.begin();
            pEnd=nickIter->second.end();
            int maxPointer=nickIter->second.size();
			uchar red, green, blue;
			Fl::get_color(Fl_Color(pIter->color), red, green, blue);

            if (pIter!=pEnd) {

                float scaleBy=theFrame.scale/pIter->scale;
                glScalef(scaleBy,scaleBy,1);
                glEnable(GL_BLEND);
                glDisable ( GL_TEXTURE_RECTANGLE_ARB );
                glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
                fader=pIter->fadeCounter;

				
				//Fl::get_color(Fl_Color(remotePointerColor), red, green, blue);
				
                glColor4f ( red,green,blue,  fader);



				//instead of a point, draw a nice crosshair?
                glPointSize ( remotePointerSize );
                glBegin ( GL_POINTS );
                glVertex3f ( pIter->x, pIter->y,0 );
                glEnd();

				/*


				*3		*2



					*1


				*/

				/*float crossHeight=remotePointerSize*5.0;
				float crossWidth=crossHeight/20.0;
				

				glBegin(GL_TRIANGLES);
					glVertex3f(pIter->x,pIter->y,0);
					glVertex3f(pIter->x-crossWidth,pIter->y+crossHeight,0);
					glVertex3f(pIter->x+crossWidth,pIter->y+crossHeight,0);

					glVertex3f(pIter->x,pIter->y,0);
					glVertex3f(pIter->x-crossWidth,pIter->y-crossHeight,0);
					glVertex3f(pIter->x+crossWidth,pIter->y-crossHeight,0);

					glVertex3f(pIter->x,pIter->y,0);
					glVertex3f(pIter->x-crossHeight,pIter->y+crossWidth,0);
					glVertex3f(pIter->x-crossHeight,pIter->y-crossWidth,0);

					glVertex3f(pIter->x,pIter->y,0);
					glVertex3f(pIter->x+crossHeight,pIter->y+crossWidth,0);
					glVertex3f(pIter->x+crossHeight,pIter->y-crossWidth,0);

				glEnd();*/
				

                glRasterPos3f ( pIter->x+remotePointerSize, pIter->y+remotePointerSize,0 );
                gl_font(FL_HELVETICA,remotePointerFontSize);
                gl_draw ( pIter->name.c_str() );

            }

            glLineWidth(remotePointerSize);

            glBegin ( GL_LINE_STRIP );
            for (pIter;pIter!=pEnd;pIter++) {

                glColor4f ( red,green,blue,  1-(float)counter/(float)maxPointer);
                //printf("%f\n",1.0-(float)counter/(float)maxPointer);
                glVertex3f ( pIter->x, pIter->y,0 );
                counter++;


            }
            glEnd();




            //for each pointer, draw the every point as a vertex, only scale on the first one.
            glPopMatrix();
        }


        glPopAttrib();
    }

    return;
}

/**
 *
 * @param params is a pinga
 */
void gfcPlate::drawForFX ( PlateFXParams params ) { //THIS METHOD WILL BE VERY DUMB, all it will do is draw a quad, with size and tex coords specified by the object passed as a parameter to the method (params)
    //The params will be filled out by each FXs bind method, since it knows exactly what is needed for each pass.


    switch ( params.pass ) {

    case FXPASS_FIRST: { { //draw a normal plate but with no transforms.
            glDisable(GL_TEXTURE_RECTANGLE_ARB);
            glDisable(GL_TEXTURE_2D);
            glEnable(target);

            glBindTexture(target, theFrame.textureID);
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,sett.filterMin);
            glTexParameteri(target,GL_TEXTURE_MAG_FILTER,sett.filterMax);
            glColor4f(0,0,0,1);

            gfcRectangf tmpTexCoords=theFrame.texCoords;
			//printf("calculatePolySizes etc tex coords: %f %f %f %f\n",tmpTexCoords.x, tmpTexCoords.y, tmpTexCoords.w, tmpTexCoords.h);
			gfcRectangf texCoords = this->textureSize;
            //printf("tmpTexCoords: %f %f %f %f\n",tmpTexCoords.x,tmpTexCoords.y,tmpTexCoords.w,tmpTexCoords.h);
			//printf("texCoords: %f %f %f %f\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h);
            glBegin(GL_QUADS);

            glTexCoord2f(tmpTexCoords.x,tmpTexCoords.y);
            glVertex3f(-texCoords.w/2.0,texCoords.h/2.0,1);

            glTexCoord2f(tmpTexCoords.w,tmpTexCoords.y);
            glVertex3f(texCoords.w/2.0,texCoords.h/2.0,1);

            glTexCoord2f(tmpTexCoords.w,tmpTexCoords.h);
            glVertex3f(texCoords.w/2.0,-texCoords.h/2.0,1);


            glTexCoord2f(tmpTexCoords.x,tmpTexCoords.h);
            glVertex3f(-texCoords.w/2.0,-texCoords.h/2.0,1);

            glEnd();
            glColorMask(1,1,1,1);
        }
        //END OF DRAW THE TEXTURED POLY

    }
    break;

    case FXPASS_INTERMEDIATE: { //This is the DUMB pass, multiple tex coords are taken from the params, texture bidings are already set in the shaders.
        //Rectang texCoords;
        /*
           1x,y-----------2s,y
           *              *
           *              *
           *              *
           4x,t-----------3s,t
           */

        glColor4f ( 0,1,0,1 );
        //glColorMask(true,true,true,true);

        glBegin ( GL_QUADS );
        //glColor3f(1,0,0);
        for ( int i=0;i<params.numberOfTextures;i++ ) {
            //printf("texCoords 0,0 - %i: %f,%f\n",i,params.texCoords[i].x,params.texCoords[i].y);
            glMultiTexCoord2f ( GL_TEXTURE0+i,params.texCoords[i].x,params.texCoords[i].y );
        }
        glVertex3f ( -fboVP.w/2.0,fboVP.h/2,1 );


        for ( int i=0;i<params.numberOfTextures;i++ ) {
            //printf("texCoords 1,0 - %i: %f,%f\n",i,params.texCoords[i].s,params.texCoords[i].y);
            glMultiTexCoord2f ( GL_TEXTURE0+i,params.texCoords[i].s,params.texCoords[i].y );
        }
        glVertex3f ( fboVP.w/2.0,fboVP.h/2,1 );

        for ( int i=0;i<params.numberOfTextures;i++ ) {
            // printf("texCoords 1,1 - %i: %f,%f\n",i,params.texCoords[i].s,params.texCoords[i].t);
            glMultiTexCoord2f ( GL_TEXTURE0+i,params.texCoords[i].s, params.texCoords[i].t );
        }
        glVertex3f ( fboVP.w/2,-fboVP.h/2,1 );


        for ( int i=0;i<params.numberOfTextures;i++ ) {
//             printf("texCoords 0,1 - %i: %f,%f\n\n\n",i,params.texCoords[i].x,params.texCoords[i].t);
            glMultiTexCoord2f ( GL_TEXTURE0+i,params.texCoords[i].x, params.texCoords[i].t );
        }
        glVertex3f ( -fboVP.w/2,-fboVP.h/2,1 );

        glEnd();
        // glPopMatrix(); //FIXME : What the hell is this pop matrix?
    }

    break;
	
	//this case is used to draw one last time to the 8bit histogram FBO, almost the same as the FXPASS_LAST, but using the whole texture size, not the the possibly
	//shrunken polySize.
	case FXPASS_HISTOGRAM_FBO:
		{
			//glColorMask(rMask | aMask,gMask | aMask,bMask | aMask,1);
			glEnable(GL_TEXTURE_RECTANGLE_ARB);
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB, params.FBOTextureID);
			glTexParameteri(target,GL_TEXTURE_MIN_FILTER,sett.filterMin);
			glTexParameteri(target,GL_TEXTURE_MAG_FILTER,sett.filterMax);
			glColor4f(0,0,0,1);

			Rectang tmpTexCoords=fboVP; //theFrame.texCoords; //we don't use the frame's texture coordinates in the last pass because they might be normalized if we use texture compression. So we use the fboVP size as texture coordinates.
			//printf("texCoords: %f %f %f %f\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h);
			glBegin(GL_QUADS);

			glTexCoord2f(tmpTexCoords.x,tmpTexCoords.y);
			glVertex3f(-texCoords.w/2.0,texCoords.h/2.0,1);

			glTexCoord2f(tmpTexCoords.w,tmpTexCoords.y);
			glVertex3f(texCoords.w/2.0,texCoords.h/2.0,1);

			glTexCoord2f(tmpTexCoords.w,tmpTexCoords.h);
			glVertex3f(texCoords.w/2.0,-texCoords.h/2.0,1);


			glTexCoord2f(tmpTexCoords.x,tmpTexCoords.h);
			glVertex3f(-texCoords.w/2.0,-texCoords.h/2.0,1);
			glEnd();
			glColorMask(1,1,1,1);
		}

    case FXPASS_LAST: { { //This pass simply maps the FBOTexture into a transformed quad. Like the regular pass but not taking into account the slices and texture coordinates from the track.


            //glColorMask(rMask | aMask,gMask | aMask,bMask | aMask,1);
            glEnable(GL_TEXTURE_RECTANGLE_ARB);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, params.FBOTextureID);
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,sett.filterMin);
            glTexParameteri(target,GL_TEXTURE_MAG_FILTER,sett.filterMax);
            glColor4f(0,0,0,1);

            Rectang texCoords=fboVP; //theFrame.texCoords; //we don't use the frame's texture coordinates in the last pass because they might be normalized if we use texture compression. So we use the fboVP size as texture coordinates.
            //printf("texCoords: %f %f %f %f\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h);
            glBegin(GL_QUADS);

            glTexCoord2f(texCoords.x,texCoords.h);
            glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);

            glTexCoord2f(texCoords.w,texCoords.h);
            glVertex3f(polySizeX/2.0,polySizeY/2.0,1);

            glTexCoord2f(texCoords.w,texCoords.y);
            glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);


            glTexCoord2f(texCoords.x,texCoords.y);
            glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);

            glEnd();
            glColorMask(1,1,1,1);


        }
        //END OF DRAW THE TEXTURED POLY




    }
    break;
    }
}




void gfcPlate::draw() {
    //1. Detect what we are rendering for, either just normal drawing, or drawing with FXs (when the fxStack reports that fxs are active) when displaying FXs or Rendering.
    drawingChoice=0; //how to draw: normally, with FX, or for render.
    if ( fxStack.getNumOfActiveFXs() || forRender ) {
        drawingChoice=1;
    } else {
        drawingChoice=0;
    }

    if (renderModeSelection==1) {
        drawingChoice=2;
    }

    currentFrame=playbackManager.getCurrentFrame();

    //2. Draw Accordingly.
    switch (drawingChoice) {
    case 0: //draw normally
        draw3Drect(currentFrame);
        break;

    case 1: //draw with FXs to a FBO;
        draw3DrectWithFX(currentFrame);
        break;

    case 2:
        //only draws objects that are selectable, the Histogram for example, the crop points, eventually the FXs GUI controls as well.
        drawForSelection(currentFrame);
        break;

    }

}



void gfcPlate::updateRot ( float timeStep, bool flip, bool flop ) {
    float animSpeed=500;
    bool redrawFlag=false;
    if ( rX<180 && flip ) {
        rX+=animSpeed*timeStep;
        redrawFlag=true;
    }
    if ( rX>180 ) {
        rX=180;
        plateManager.setChanged();
    }


    if ( rX>0 && !flip ) {
        rX-=animSpeed*timeStep;
        redrawFlag=true;
    }
    if ( rX<0 ) {
        rX=0;
        plateManager.setChanged();
    }

	if ( rY<180 && flop ) {
        rY+=animSpeed*timeStep;
        redrawFlag=true;
    }
    if ( rY>180 ) {
        rY=180;
        plateManager.setChanged();
    }


    if ( rY>0 && !flop ) {
        rY-=animSpeed*timeStep;
        redrawFlag=true;
    }
    if ( rY<0 ) {
        rY=0;
        plateManager.setChanged();
    }

    rotationsTimeSinceLastRedraw+=timeStep;
    if (rotationsTimeSinceLastRedraw>=0.003 && redrawFlag) {
        plateManager.setChanged();
        rotationsTimeSinceLastRedraw=0;
    }

}

void gfcPlate::draw3DrectWithFX(int pcurrentFrame) {
    if (!myGUI)
        return;

    PlateFXParams params;
    currentFrame=pcurrentFrame;
    target=GL_TEXTURE_RECTANGLE_ARB;
    areaOfIntrestOn=false;


    //************GET THE GFCFRAME AND WRITE THE LABEL
    {
        getFrameAndSequence();
    }

    //********set the viewport for this plate************
    startViewport();

    //END OF GET THE GFCFRAME AND WRITE THE LABEL
    if ( !sett.fbo || !sett.glsl) {
        draw3Drect();
        gl_draw ( "Your hardware configuration can't make use of JefeCheck's FX functionality" );
    }
    if (theFrame.loaded) {

        //*********CALCULATE POLY SIZES, CROP BARS ETC***********
        calculatePolySizesCropEtc();

        //Draw to the FBO for each FX in the stack
        {

            {

                {


                    /***************************************************
                    FIRST PASS:
                    ALWAYS render the assigned track into the FBO with the fbo having the same resolution as the track.
                    This quad will not have any transforms so that all subsequent passes can draw to a polygon the same size with
                    the same texture coordinates (the transforms to draw to the screen will only be applied on the last fx

                    ISSUE: Maybe this pass could be avoided if the FXs is a compositing FX and don't require the first "pure" textured poly since they only use the textures they have assigned as params. For fxs that only modify colors or something else but don't use extra textures as params this pass would be required. UNLESS those kind of fxs could also have a texture param to use. Has to be tought through some more.
                    ****************************************************/
                    //                     printf("--------\nDrawing First Pass\n--------\n\n");
                    //compare texture size with FBO viewport size, if different, resize fboVP and createFBO

                    /*if ( forRender ) {
                        rw.progress->copy_label ( "Loading and Rendering" );
                        Fl::check();
                    }*/

                    //if ( polySizeX!=fboVP.w || polySizeY!=fboVP.h ) 
					if (this->textureSize.w!=fboVP.w || textureSize.h!=fboVP.h)					
					{
                        printf(" *fboVP different from the track size, creating new fbo! (%ix%i vs %ix%i)\n",fboVP.w,fboVP.h,polySizeX,polySizeY);
                        //fboVP.w=polySizeX;
                        //fboVP.h=polySizeY;
						fboVP.w=textureSize.w;
						fboVP.h=textureSize.h;
                        createFBO();
                    } else {
                        //                         printf(" *fboVP: (%i,%i,%i,%i)\n",fboVP.x,fboVP.y,fboVP.w,fboVP.h);
                        //                         printf(" *fboVP already up to date\n");
                    }



                    //Draw the 1st pass poly with the assigned track's texID into the FBO texture
                    //printf(" *Enabling drawing into FBO\n  *viewport: (%i,%i,%i,%i)\n",fboVP.x,fboVP.y,fboVP.w, fboVP.h);

                    //Bind the FBO, we also need to set the vieport to the fboVP settings so that our quads fill up the fboTexture correctly
                    glPushAttrib ( GL_VIEWPORT_BIT ); //this will be poped at the end of the last pass

                    activeFBO=0;
                    glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbov[0] );
                    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+activeFBO);

                    glClearColor ( 0,0,0,1 );
                    glPushAttrib ( GL_COLOR_BUFFER_BIT );
                    glColorMask ( true,true,true,true ); //make sure we clear all the colors, but restore before drawing
                    glClear ( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
                    glPopAttrib();


                    glMatrixMode ( GL_PROJECTION );
                    glPushMatrix(); //PUSH PROJ MATRIX this will be poped at the end of the last pass
                    glLoadIdentity();
                    glOrtho ( -fboVP.w/2.0, fboVP.w/2.0, -fboVP.h/2.0, fboVP.h/2.0, -5000.0, 5000.0 );

                    glMatrixMode ( GL_MODELVIEW );
                    //glPushMatrix(); //PUSH MODELVIEW MATRIX this will be poped at the end of the last pass
                    glLoadIdentity();
                    glViewport ( fboVP.x,fboVP.y,fboVP.w, fboVP.h ); //set the viewport to be used on the FBO

                    params.pass=FXPASS_FIRST;
                    params.currentFrame=currentFrame;
                    drawForFX ( params );

                    //Note: Keep the FBO target active...
                    //Note: Now we actually leave the 2nd FBO active to render to, while using the 1rst one's texture to read from.
                    /*-----------------END OF FIRST PASS-----------------*/


                    /***************************************************
                    SECOND TO fxArray.size -1 PASSES:
                    For each effect but the last one draw a poly the same size without transformation, binding each fx and it's corresponging params and textures into the shader. The textures can be "pure" texIDs obtained from tracks, or the "last result" obtained from the FBO texture. The last fx (fxArray.size) is not drawn in this loop, since it's output will go directly into the Color Buffer, and not into the FBO.

                    ISSUE: It's possible that two FBO (or at least two render targets within the same FBO) will be needed because we might be reading the previous result to calculate and draw into the next result at the same time. A simple back/front buffer technique will do, simply changing the draw-to and read-from on each iteration, without doing any swapping or slow copies. UPDATE> EVERYTHING SEEMS TO BE WORKING FINE AND THERE ARE NO CONFLICTS APPARENTLY, very strange....
                    ****************************************************/
                    //JUST A TEST: with the binded fboTexture, draw to the FBO and move on to next step
                    for ( int i=0; i<fxStack.getNumOfFXs();i++ ) {

                        gfcFX theFX=fxStack.getFX(i);
                        if ( theFX.active ) {
                            //"swap" the active FBO
                            activeFBO=!activeFBO;

                            //when forcing single buffered FBOs we always use activeFBO 0 (it will be ! when used)


                            //when forcing single buffered FBOs we always use activeFBO 0 (it will be ! when used)
                            if (forceSingleBufferedFX)
                                glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbov[0]);
                            else
                                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+activeFBO);

                            //printf("Intermediate Writing To: %i\n",activeFBO);
                            bool fxUsingTrackTexture=false;
                            //printf("Binding FX[%i][%i]: %s\n",quadID,i,fxApplied[quadID][i].name);
                            FXTexCoords fboTexCoords;
                            fboTexCoords.s=fboVP.w;
                            fboTexCoords.y=fboVP.h;
                            fboTexCoords.x=0.0;
                            fboTexCoords.t=0.0;


                            //when forcing single buffered FBOs we always use activeFBO 0 (it will be !'ed when used)
                            if (forceSingleBufferedFX)
                                params=theFX.bind ( fboTexturev[0],fboTexCoords, forRender ); //fxUsingTrackTexture will be true if a track texture is used. insetad of the fbo.
                            else
                                params=theFX.bind ( fboTexturev[!activeFBO],fboTexCoords, forRender ); //fxUsingTrackTexture will be true if a track texture is used. insetad of the fbo.
                            //printf("Intermediate Reading From: %i\n",!activeFBO);
                            //printModelView();

                            glEnable ( GL_TEXTURE_RECTANGLE_ARB );

                            drawForFX ( params );
                            // printViewport();
                            glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,0 );
                            glDisable ( GL_TEXTURE_RECTANGLE_ARB );
                            theFX.unbind();

                        }

                    }

                    //IT WORKS! GREAT!

                    /*--------------END OF SECOND TO fxArray.size-1 PASSES ------------------*/





                }
            }



        }

        if (forceSingleBufferedFX) {
            activeFBO=0;
            //std::cout << "single buffered!\n" ;
        }

        if ( forRender ) {
            //instance an imageSaver according to format
            renderParams.sizeX=fboVP.w;
            renderParams.sizeY=fboVP.h;
            gfcImageSaver *imageSaver= getImageSaverInstance(renderParams);

            //get the FBOs texture into the imageSavers pixels;
            void * thePixels=imageSaver->getPixelPointer();
            if (thePixels==NULL) {
                printf("ERROR: %s\n",imageSaver->getErrorString().c_str());
                imageSaver->freeResources();
                delete imageSaver;
                return;

            }

            glPrintError();
            //printf("Before getTexImage\n");
            glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,fboTexturev[activeFBO] );
            glGetTexImage ( GL_TEXTURE_RECTANGLE_ARB,0,imageSaver->getGLFormat(),imageSaver->getGLPixelFormat(),thePixels );
            //printf("The error should follow\n");
            glPrintError();
            //glFinish();
            imageSaver->save();
            imageSaver->freeResources();

            if (imageSaver)
                delete imageSaver;

 


            /*if(thePixels)
            	delete [] thePixels;*/


            /* mw.vp->trackA.cleanForcedLoad();
             mw.vp->trackB.cleanForcedLoad();
             mw.vp->trackC.cleanForcedLoad();
             mw.vp->trackD.cleanForcedLoad();*/
            //printf("deleted!\n");

        }

        //draw the FBO to the screen.
        /***************************************************
            LAST PASS (fxArray.size) :
            The last pass renders binds the last fx, and instead of rendering into the FBO, it renders into the screen.
            This quad is rendered with the transformations, aspect ratio and crops, and info texts.
        ****************************************************/


        if (histogramWindow.visible()) {
            //only rerender to this texture if the frame is not cached.
            if (histogramCache.count(currentFrame)==0) {
                int textureAttachmentIndex=fboTexturevCount-1;
                //Before poping the attributes to revert to the real viewport screen, render it once more but to the 8bit FBO
                if (!multiFormatFBOSupported) {
                    glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, fbo8bit);
                    //printf("Rendering to fbo8bit\n");
                    textureAttachmentIndex++;
                }

                glPrintError();
                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+textureAttachmentIndex); //draw to the last color attachment, the 8 bit texture.
                glPrintError();
                {
                    //Call the drawForFX method with parameters to draw the third pass
                    //printf("Drawing 8 bit texture to color attachment %i\n",textureAttachmentIndex);
                    params.pass=FXPASS_HISTOGRAM_FBO;
                    params.FBOTextureID=fboTexturev[activeFBO];

                    drawForFX ( params );
                    glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,0 );
                    glDisable ( GL_TEXTURE_RECTANGLE_ARB );
                }
            }


        }
        //disable the FBO render target
        glBindFramebufferEXT ( GL_FRAMEBUFFER_EXT, 0 );
        glPopAttrib(); //restore the viewport to the real viewport on screen, not the viewport of the FBO.


        //glMatrixMode ( GL_MODELVIEW );
        //glPopMatrix(); //POP MODELVIEW MATRIX

        glMatrixMode ( GL_PROJECTION );
        glPopMatrix(); //POP PROJ MATRIX*/

        glMatrixMode ( GL_MODELVIEW ); //the modelview matrix should be active last
        glLoadIdentity();

        {
            //Call the drawForFX method with parameters to draw the third pass

            params.pass=FXPASS_LAST;
            params.FBOTextureID=fboTexturev[activeFBO];
            //printf("Last Reading From: %i\n\n\n",activeFBO);

            //*********TRANSFORM WHATHEVER WE WILL DRAW**********

            startTransform();
            startAlphaBackground();
			//START SUPER SHADER HERE
			textureTypeForSuperShader=theFrame.compressed!=GFC_S3TCDX1?theFrame.compressed:GFC_8BPC; //really it only has to be different than S3TC...
			textureIDforSuperShader=params.FBOTextureID;
			startSuperShader();
            drawForFX ( params );
            stopAlphaBackround();
			stopSuperShader();
            glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,0 );
            glDisable ( GL_TEXTURE_RECTANGLE_ARB );
        }
		
        drawCropBars();
        drawDO();
        capturePointerCoords();
        drawRemotePointers();
        //**********DRAW THE AOI OVERLAY******
        drawAOIOverlay();
        //**********RESTORE THE MODELVIEW MATRIX******
        endTransform();

    }
    //**********DRAW THE LABEL*****************
    drawText();

    //**********DRAW THE HISTOGRAM AND VECTORSCOPE*****************
    drawHistogram();
    drawVectorscope();

    if ( forRender ) { //remember to clean up the forcedFrame if we used it for render.
        //release any forced load images.
        trackManager.cleanForcedLoaded();
        //theSequence->cleanForcedLoad();
    }

    endViewport();
}

void gfcPlate::draw3Drect(int pcurrentFrame) {
    if (!myGUI)
        return;

    currentFrame=pcurrentFrame;
    target=GL_TEXTURE_RECTANGLE_ARB;
    areaOfIntrestOn=false;
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_TEXTURE_2D);

    //************GET THE GFCFRAME AND WRITE THE LABEL
    {
        getFrameAndSequence();
    }
    //END OF GET THE GFCFRAME AND WRITE THE LABEL


    //********set the viewport for this plate************
    startViewport();

    if (theFrame.loaded) {
        //*********CALCULATE POLY SIZES, CROP BARS ETC***********
        calculatePolySizesCropEtc();
        //*********TRANSFORM WHATHEVER WE WILL DRAW**********
        startTransform();
		
		

        //***********WHITE BACKGROUND FOR ALPHA*****************
        //startAlphaBackground();

		
        //**********DRAW THE TEXTURED POLY**********

        {
		/*	
            glEnable(target);
			
            if (rMask || gMask || bMask) {
                glBindTexture(target, 0);
                glColor4f(0,0,0,1);

                texCoords=theFrame.texCoords;
                //printf("texCoords: %f %f %f %f, target==%s\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h,target==GL_TEXTURE_RECTANGLE_ARB?"GL_TEXTURE_RECTANGLE_ARB":"GL_TEXTURE_2D");
                glBegin(GL_QUADS);

                glTexCoord2f(texCoords.x,texCoords.y);
                glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);

                glTexCoord2f(texCoords.w,texCoords.y);
                glVertex3f(polySizeX/2.0,polySizeY/2.0,1);

                glTexCoord2f(texCoords.w,texCoords.h);
                glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);


                glTexCoord2f(texCoords.x,texCoords.h);
                glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);

                glEnd();
                glColorMask(1,1,1,1);
            }*/
			
            //glColorMask(rMask | aMask,gMask | aMask,bMask | aMask,1); REENABLE THIS WHEN NOT USING SHADERS FOR RGB
            /*if(!glIsTexture(theFrame.textureID))
            {   //TODO:use the ollin texture quad
            	//printf("Not a texture!!!!\n");
            }*/

            glEnable(target);
            glBindTexture(target, theFrame.textureID);
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,sett.filterMin);
            glTexParameteri(target,GL_TEXTURE_MAG_FILTER,sett.filterMax);


            glColor4f(0,0,0,1);
			//START SUPER SHADER HERE
			//bind SuperShader
			textureTypeForSuperShader=theFrame.compressed;
			textureIDforSuperShader=theFrame.textureID;
			startSuperShader();
            texCoords=theFrame.texCoords;
            //printf("texCoords: %f %f %f %f\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h);
            glBegin(GL_QUADS);

            glTexCoord2f(texCoords.x,texCoords.y);
            glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);

            glTexCoord2f(texCoords.w,texCoords.y);
            glVertex3f(polySizeX/2.0,polySizeY/2.0,1);

            glTexCoord2f(texCoords.w,texCoords.h);
            glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);


            glTexCoord2f(texCoords.x,texCoords.h);
            glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);

            glEnd();
			stopSuperShader();
            glColorMask(1,1,1,1);
        }
        //END OF DRAW THE TEXTURED POLY
        //Stop the effects we needed for the alpha background
        //stopAlphaBackround();
        //**********DRAW THE CROP BARS************
        drawCropBars();
        drawDO();
        capturePointerCoords();
        drawRemotePointers();
        //**********DRAW THE AOI OVERLAY******
        drawAOIOverlay();
        //**********RESTORE THE MODELVIEW MATRIX******
        endTransform();


    }

    //draw the warning in case we don't support texture rectangles
    drawTextureRectangleWarning();

    //**********DRAW THE LABEL*****************
    drawText();

    //**********DRAW THE HISTOGRAM AND VECTORSCOPE*****************
    drawHistogram();
    drawVectorscope();

    //********restore the viewport************
    endViewport();
}


void gfcPlate::resetColorCorrection()
{
	this->setLUT(-1);
	this->setGamma(1.0);
	this->setExposure(0.0);
	this->setBrightness(1.0);
	this->setContrast(1.0);
	this->setSaturation(1.0);
	plateManager.setFeedbackMessage("Reset Color Correction");
}

void gfcPlate::resetTransforms(void) {
    tX=tY=rZ=rX=rY=0;
    scale=1.0;
	
	myGUI->setTX(tX);
	myGUI->setTY(tY);
	myGUI->setRZ(rZ);
	myGUI->setScale(scale);

    /*panPlate(0.0,0.0);
    zoomPlate(0.0);
	rotatePlate(0.0)*/

    myGUI->setFlip(false);
    flip=false;
    myGUI->setFlop(false);
    flop=false;
	plateManager.setFeedbackMessage("Reset Transformations");
}

#include "demoversion.h"
extern GLuint gWatermarkTextureID;
void gfcPlate::drawDO() {
#ifdef DEMO_VERSION
	/*if ((this->theFrame.textureID%30>5))
		return;*/

    glPushAttrib(GL_ALL_ATTRIB_BITS);
	
    //glColorMask(true,true,true,true);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, gWatermarkTextureID);

    glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
    glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );

    glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
    glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );

   /* glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT );
    glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);*/
	glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
	/*glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_MIRRORED_REPEAT );
	glTexParameteri ( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_MIRRORED_REPEAT);*/

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	
    glEnable(GL_BLEND);
	/*if ((this->theFrame.textureID%9))
	{
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA,GL_SRC_ALPHA);
	}
	else*/
	{
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}
    
	
 

  	//THIS COMBINATION WORKS TO TILE CORRECTLY
	//float textureCoordX=polySizeX/(float)GWATERMARKDATAWIDTH;
	//float textureCoordY=polySizeY/(float)GWATERMARKDATAHEIGHT;

	float nm=(float)polySizeX/(float)polySizeY;
	float offset=-(nm/2.0-0.5); 
	float textureCoordX=nm+offset;//nm+0.25*nw;
	float textureCoordY=1.0; //Y stetches to the top
	float texOriginX=0+offset;//-0.25*polySizeX;//-0.5*textureCoordX;
    
	//printf("textureCoordX=%f, textureCoordY=%f\n texOriginX=%f, nw=%f , nm=%f\n",textureCoordX,textureCoordY,texOriginX,nw,nm);

	int size=polySizeY;
	
    int pos=polySizeX/2;//((this->theFrame.textureID%30)+1);
    
	glPushMatrix();
	int fullTurn=120;
	//int theCounter=playbackManager.getCurrentFrame();
	int theCounter=theFrame.indexNumber+180;
	float wmRotation=(theCounter%fullTurn)/float(fullTurn)*360.0;
	//float theCos=(cos(wmRotation*0.0174532925 )+1)/2.0;
	float theCos=1-(fabs(cos(wmRotation*0.0174532925 )));
	//printf("wmRotation=%f, theCos=%f\n",wmRotation,theCos);

	//glRotatef(wmRotation,0,1,0);
	glColor4f(1.0,1.0,1.0,theCos);
    glBegin(GL_QUADS);
	

    glTexCoord2f(texOriginX,0);
    //glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);

    glVertex3f(-pos,size/2.0,1);

    glTexCoord2f(textureCoordX,0);
    //glVertex3f(polySizeX/2.0,polySizeY/2.0,1);
    glVertex3f(pos,size/2.0,1);

    glTexCoord2f(textureCoordX,textureCoordY);
    //glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);
    glVertex3f(pos,-size/2.0,1);

    glTexCoord2f(texOriginX,textureCoordY);
    //glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);
    glVertex3f(-pos,-size/2.0,1);

    glEnd();
	glPopMatrix();

    glPopAttrib();

#endif
}

/*!
    \fn gfcPlate::drawCropBars(gfcFrame* theFrame)
 */
void gfcPlate::drawCropBars() {

    if (cropOn) {
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0,0,0,sett.aspectBarsOpacity);
        glDisable(target);
        glBegin(GL_QUADS);

        glVertex3f(cropBarTop.x,cropBarTop.y,15);
        glVertex3f(cropBarTop.x,cropBarTop.h,15);
        glVertex3f(cropBarTop.w,cropBarTop.h,15);
        glVertex3f(cropBarTop.w,cropBarTop.y,15);
        glEnd();

        glBegin(GL_QUADS);

        glVertex3f(cropBarBottom.x,cropBarBottom.y,15);
        glVertex3f(cropBarBottom.x,cropBarBottom.h,15);
        glVertex3f(cropBarBottom.w,cropBarBottom.h,15);
        glVertex3f(cropBarBottom.w,cropBarBottom.y,15);
        glEnd();
		glPopAttrib();
    }
}


/*!
    \fn gfcPlate::drawAOIOverlay(gfcFrame* theFrame)
 */
void gfcPlate::drawAOIOverlay() {

    if (aoi.x==-1 || aoi.y==-1 || aoi.h==-1 || aoi.w==-1) {
        return;
    } else {

		
        glPushMatrix();
        glLoadIdentity();
        glScalef(scale,scale,scale);
        glTranslatef(tX,tY,0);

        //aoi.x=aoi.y=-350;
        //aoi.w=aoi.h=350;
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
        glDisable(GL_TEXTURE_2D);
        if (drawingChoice!=2){
		glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else
		{
		glDisable(GL_BLEND);
		}
		
		int x0toDraw=aoi.x-theFrame.sizeX/2;
		int y0toDraw=aoi.y-theFrame.sizeY/2;

		if (drawingChoice!=2){
		glColor4f(0.6,0.6,0.9,0.2);
		}
		else{
		glColor3ubv((GLubyte*)aoiPickMove.getPickColor().colors);
		}

        glBegin(GL_QUADS);
		
        glVertex3f(x0toDraw,y0toDraw,0);

        glVertex3f(x0toDraw,y0toDraw+aoi.h,0);

        glVertex3f(x0toDraw+aoi.w,y0toDraw+aoi.h,0);

        glVertex3f(x0toDraw+aoi.w,y0toDraw,0);

        glEnd();

        
		
        glPointSize(5);
        
		if (drawingChoice!=2){
		glColor3f(1,1,1);
		glBegin(GL_POINTS);
        glVertex3f(x0toDraw,y0toDraw,0);

        glVertex3f(x0toDraw,y0toDraw+aoi.h,0);

        glVertex3f(x0toDraw+aoi.w,y0toDraw+aoi.h,0);

        glVertex3f(x0toDraw+aoi.w,y0toDraw,0);
		glEnd();
		}
		else
		{	//DRAW THE PICK POINTS
			glPointSize(7); //give a little room for error when picking
			glBegin(GL_POINTS);
			glColor3ubv((GLubyte*)aoiPickSW.getPickColor().colors);
			glVertex3f(x0toDraw,y0toDraw,0);

			glColor3ubv((GLubyte*)aoiPickNW.getPickColor().colors);
			glVertex3f(x0toDraw,y0toDraw+aoi.h,0);

			glColor3ubv((GLubyte*)aoiPickNE.getPickColor().colors);
			glVertex3f(x0toDraw+aoi.w,y0toDraw+aoi.h,0);

			glColor3ubv((GLubyte*)aoiPickSE.getPickColor().colors);
			glVertex3f(x0toDraw+aoi.w,y0toDraw,0);
			glEnd();

		}
		
		if (drawingChoice!=2)
		{
		
		
        //draw start and end points text
        char temp[800];
        glColor3f(1,1,1);
        gl_font(FL_HELVETICA,10);
        sprintf(temp," ( %i,%i ) ",aoi.x,aoi.y);
        gl_draw(temp,x0toDraw-5,y0toDraw-5);


        sprintf(temp," ( %i,%i ) ",aoi.x+aoi.w,aoi.y+aoi.h);
        gl_draw(temp,x0toDraw+aoi.w+5,y0toDraw+aoi.h+5);

		}

        // poly.draw(temp);

        glDisable(GL_BLEND);

        glPopMatrix();
		
		if(drawingChoice!=2){
			char temp[800];
        //DRAW AREA OF INTEREST INFO
        sprintf(temp,"%ix%i: %.1f%%",
                aoi.w,aoi.h,
                ((float)aoi.w*aoi.h)/((float)theFrame.sizeX*theFrame.sizeY)*100.0);

        gl_font(FL_HELVETICA_BOLD,10);
        gl_draw(temp,
                x0toDraw+5, y0toDraw+5,  aoi.w, aoi.h,
                Fl_Align(FL_ALIGN_CENTER));

        /*sprintf(temp,"\n\n\n\nRight Click-Drag to Move\nShift+Righ Click-Drag to Resize");
        gl_font(FL_HELVETICA_BOLD,10);
        gl_draw(temp,
                x0toDraw, y0toDraw,  aoi.w, aoi.h,
                Fl_Align(FL_ALIGN_CENTER));*/

		//gl_rectf( x0toDraw, y0toDraw,  aoi.w, aoi.h);
		
		}
        

    }


}

void gfcPlate::zoomPlate(float zoom) {
    if (!histogramWindow.picked) {
	scale+=zoom;
    myGUI->setScale(scale);
	plateManager.setFeedbackMessage(std::string("zoom:")+ftos(scale,2));	
	}
}

void gfcPlate::rotatePlate(float rotation) {
	if (!histogramWindow.picked) {
		rZ+=rotation;
		myGUI->setRZ(rZ);
	}
	plateManager.setFeedbackMessage(std::string("rotation:")+ftos(scale,2));
}

void gfcPlate::panPlate(float panX, float panY) {
    if (!histogramWindow.picked) {
        if (scale!=0) {
            tX-=(panX)/scale;
            tY+=(panY)/scale;
        }

        myGUI->setTX(tX);
        myGUI->setTY(tY);
    }
}

void gfcPlate::updateColorCorrectionValuesFromGUI()
{
	if (myGUI!=NULL) {
		
		int theLutIndex=myGUI->getLUT()-1;

		CubeLUT tmpLut;
		if (theLutIndex>=0)
		{
			tmpLut=lutManager.getLUT(theLutIndex);
		}
		else
		{
			tmpLut.name="NOT LOADED";
			tmpLut.texture1D=-1;
			tmpLut.texture3D=-1;
			tmpLut.type=CubeLUT::JEFECHECK1D;
		}

		//printf("Updated LUT value to type: %i, name %s\n",tmpLut.type,tmpLut.name);
		this->lutType=tmpLut.type;
		if (tmpLut.type==CubeLUT::JEFECHECK1D)
		{
			this->lutID=tmpLut.texture1D;
		}
		else
		{
			this->lutID=tmpLut.texture3D;
		}
		
		this->lutSize=tmpLut.size;

		this->gamma=myGUI->getGamma();
		this->exposure=myGUI->getExposure();
		this->brightness=myGUI->getBrightness();
		this->contrast=myGUI->getContrast();
		this->saturation=myGUI->getSaturation();
		
	}
}

void gfcPlate::updateTransformationValuesFromGUI()
{
	if (myGUI!=NULL) {
		this->scale=myGUI->getScale();
		this->tX=myGUI->getTX();
		this->tY=myGUI->getTY();
		this->rZ=myGUI->getRZ();
	}
}

void gfcPlate::updateValuesFromGUI() {
//TODO: consider dividing this function into many functions so we only update a specific value and not every GUI element.

//has to read the myGUI values and set them in the plate values. We don't want to read from the GUI everytime we need the value, it is probably much slower.
    if (myGUI!=NULL) {
        this->cropOn=myGUI->getCrop();
        this->aspect=myGUI->getAspect();
        this->track=myGUI->getSequenceID();
        this->rMask=myGUI->getChannelR();
        this->gMask=myGUI->getChannelG();
        this->bMask=myGUI->getChannelB();
        this->aMask=myGUI->getChannelA();
        this->flip=myGUI->getFlip();
        this->flop=myGUI->getFlop();
        this->showPreview=myGUI->getShowPreview();
		
		this->updateColorCorrectionValuesFromGUI();
		this->updateTransformationValuesFromGUI();
    }
}

void gfcPlate::updateValueToGUI() {
//TODO: consider dividing this function into many functions so we only update a specific value and not every GUI element.
//take the plate values and set the myGUI values accordingly.
    if (myGUI!=NULL) {
        myGUI->setScale(scale);
        myGUI->setTX(tX);
        myGUI->setTY(tY);
    }
}


void gfcPlate::toggleTextMode(int reset) {
    textMode=(textMode+1)%3;
    if (reset) {
        textMode=0;
    }
    //printf("textMode %i: %i\n",this->quadID,textMode);
}

void gfcPlate::toggleHistogramMode(int reset) {
    histogramWindow.visible((histogramWindow.visible()+1)%2);
    if (reset) {
    	histogramWindow.visible(0);
    }
    
}

void gfcPlate::calculatePolySizesCropEtc() {


    if (aspect==-1 || cropOn) {//Use the original size from file when aspect is -1 (file) or when we will do cropping
		textureSize.w = theFrame.sizeX;
		textureSize.h = theFrame.sizeY;

        polySizeX=theFrame.quadSizeX;
        polySizeY=theFrame.quadSizeY;

        if (cropOn && aspect!=-1) {
            switch (cropMode) {
            case GFCCROPCENTER:

                cropBarBottom.x=-(int)(polySizeX/2.0); //bottom corner
                cropBarBottom.y=-(int)(polySizeY/2.0);
                cropBarBottom.w=(int)(polySizeX/2.0);
                cropBarBottom.h=-(int)(polySizeY/2.0 - (polySizeY-polySizeX*aspect)/2.0);


                cropBarTop.x=-(int)(polySizeX/2.0); //Top corner
                cropBarTop.y=(int)(polySizeY/2.0);
                cropBarTop.w=(int)(polySizeX/2.0);
                cropBarTop.h=(int)(polySizeY/2.0 - (polySizeY-polySizeX*aspect)/2.0);

                break;

            case GFCCROPTOP:
                cropBarBottom.x=0; //bottom corner
                cropBarBottom.y=0;
                cropBarBottom.w=0;
                cropBarBottom.h=0;


                cropBarTop.x=-(int)(polySizeX/2.0); //Top corner
                cropBarTop.y=(int)(polySizeY/2.0);
                cropBarTop.w=(int)(polySizeX/2.0);
                cropBarTop.h=(int)(polySizeY/2.0 - (polySizeY-polySizeX*aspect));

                break;

            case GFCCROPBOTTOM:
                cropBarBottom.x=-(int)(polySizeX/2.0); //bottom corner
                cropBarBottom.y=-(int)(polySizeY/2.0);
                cropBarBottom.w=(int)(polySizeX/2.0);
                cropBarBottom.h=-(int)(polySizeY/2.0 + (polySizeY-polySizeX*aspect));


                cropBarTop.x=0; //Top corner
                cropBarTop.y=0;
                cropBarTop.w=0;
                cropBarTop.h=0;

                break;
            }
        } else {
            cropBarTop.set(0,0,0,0);
            cropBarBottom.set(0,0,0,0);
        }

    } else {
           if (aspect>1) {
            polySizeX=(int)(theFrame.sizeY/aspect);
            polySizeY=(int)(theFrame.sizeY);
        } else {
            polySizeX=(int)(theFrame.sizeX);
            polySizeY=(int)(theFrame.sizeX*aspect);
        }
    }

    texCoords.x=0;
    texCoords.y=0;

    if (theFrame.compressed==GFC_S3TCDX1) {
        texCoords = theFrame.texCoords; //these are already normalized and adjusted in case we had to move to the next divisible by 4;
        target=GL_TEXTURE_2D;

    } else {
        texCoords.w=theFrame.sizeX;
        texCoords.h=theFrame.sizeY;
    }

}



void gfcPlate::drawTextureRectangleWarning() {
    if (!sett.textureRectangles) {
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(target);
        glBindTexture(target,0);
        glDisable(target);

        gl_font(FL_TIMES + FL_BOLD,textDisplaySize);
        gl_font(FL_HELVETICA + FL_BOLD,textDisplaySize);
        // gl_font(FL_COURIER + FL_BOLD,12);

        glColor4f(textDisplayColor,textDisplayColor,textDisplayColor,textDisplayOpacity);

        gl_draw("Sorry, your video hardware does not fill JefeCheck's basic requirements.\n Maybe a driver upgrade can help, but it is not likely.\n\nPlease review the minimum hardware requirements to run JefeCheck at www.jefecheck.com",
                rect.x+10,rect.y-15,
                rect.w,rect.h,

                Fl_Align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP));

        glPopAttrib();
    }


}

int gfcPlate::getHistogramFromTexture(GLuint theTexture, int sizeX, int sizeY,gfcHistogram &result, GLuint readFormat) {

    //get the texture's pixels into the
    //pixelRGBA8Bit * thePixels=new pixelRGBA8Bit[sizeX*sizeY];

    unsigned char *thePixelsc;

    thePixelsc=new unsigned char[sizeX*sizeY*4];

    if (thePixelsc==NULL) {
        printf("ERROR: could not allocate histogram image buffer\n");
        return 1;
    }



    glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,theTexture );
    glGetTexImage ( GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,readFormat,thePixelsc );
    //printf("The error should follow\n");
    glPrintError();
    result.quality=histogramQuality;
    result.processPixels(sizeX, sizeY,thePixelsc);
    if (thePixelsc!=NULL) {
        delete [] thePixelsc;
    }
    return 0;
}

void gfcPlate::updateHistogram() {
    if (histogramWindow.visible()) {}
    /*{
    	
    	//for now get the fbo, and go through it, when we update will be a subject for discution later.
    				
    	//check if the frame's histogram is dirty, When an FX or track (or other things maybe) in this plate change, it must set all the track's frames to dirty.
    	//check if the frame's histogram is dirty, When an FX or track (or other things maybe) in this plate change, it must set all the track's frames to dirty.
    	
    if(currentPlateStateHistogramIsDirty()){
    	//the image has changed since we last where here, calculate a new histogram
    	//read back the image into the
    	}
    }*/
}

void gfcPlate::drawHistogram() {

if(histogramWindow.visible()){
    
    if (theFrame.loaded) {
    	gfcHistogram theHist;
        int caching=0;
            if (drawingChoice!=2) {
            	histogramWindow.drawForPicking=0;
                if (histogramCache.count(currentFrame)==0) { //this means this frame has not been cached, get a new histogram.and that we are not picking, we should never cache a frame that was rendered from picking!

                    //TODO: Get the texture ID from the frame or from the FBOs depending on the FX stack state.
                    switch ( drawingChoice ) {
                    case 0:
						//printf("Getting histogram texture from textureID=%i (%ix%i)\n",theFrame.textureID, theFrame.sizeX, theFrame.sizeY);
                        getHistogramFromTexture(theFrame.textureID, theFrame.sizeX, theFrame.sizeY,theHist, GL_UNSIGNED_BYTE);
                        break;
                    case 1:
						//printf("Getting histogram texture from fbo8bitTexture=%i (%ix%i)\n",fbo8bitTexture, theFrame.sizeX, theFrame.sizeY);
                        getHistogramFromTexture(fbo8bitTexture, theFrame.sizeX, theFrame.sizeY,theHist, GL_UNSIGNED_BYTE);
                        break;
                    }
                    //printf("caching histogram %i\n",currentFrame);
                    caching=1;

                    histogramCache[currentFrame]=theHist;
                } else {
                    //printf("drawing cached histogram %i\n",currentFrame);
                    theHist=histogramCache[currentFrame];
                }
            } else {
            	histogramWindow.drawForPicking=1;
                //theHist.drawForPicking=1;
                //theHistogramWindow.myUniqueColor=pickManager.getUniqueColor();
                //histogramPickColor=pickManager.getUniqueColor();
                //theHist.myUniqueColor=histogramPickColor;
                //histogramCornerPickColor=pickManager.getUniqueColor();
                //theHist.myUniqueCornerColor=histogramCornerPickColor;
            }

            /*theHist.scaleX=histogramScaleX;
            theHist.scaleY=histogramScaleY;
            theHist.posX=histogramPosX;
            theHist.posY=histogramPosY;*/
	    
	    histogramWindow.draw(&theHist, caching);

            //TODO: draw the histogram
            //theHist.draw(caching);
        
    }
   }
}

void gfcPlate::drawVectorscope() {
    if (theFrame.loaded) {
        if (showVectorscope) {
        }
    }
}

void gfcPlate::drawText() {
    if (showText) {
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(target);
        glBindTexture(target,0);
        glDisable(target);
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
    	glDisable(GL_TEXTURE_2D);

        gl_font(FL_TIMES + FL_BOLD,textDisplaySize);
        gl_font(FL_HELVETICA + FL_BOLD,textDisplaySize);
        // gl_font(FL_COURIER + FL_BOLD,12);

        glColor4f(textDisplayColor,textDisplayColor,textDisplayColor,textDisplayOpacity);




        gl_draw(labelString.c_str(),
                rect.x+10,rect.y-15,
                rect.w,rect.h,

                Fl_Align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP));

        glPopAttrib();
    }
}

void gfcPlate::startTransform() {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();



    if ((rX!=0 || rY!=0 || rZ!=0)) {
        glScalef(scale,scale,scale); //SCALE THE FRAME


        glTranslatef(tX,tY,0); //MOVE THE FRAME

        glRotatef(rX,1,0,0); //ROTATE FOR FLIP, FLOP AND Z ROTATION
        glRotatef(rY,0,1,0);
        glRotatef(rZ,0,0,1);

    } else {
        glScalef(scale,scale,scale); //SCALE THE FRAME
        glTranslatef(tX,tY,0); //MOVE THE FRAME
        //NO ROTATION
    }
}

void gfcPlate::endTransform() {
    glPopMatrix();

}

void gfcPlate::startAlphaBackground() {
    if (aMask) {

        glPushAttrib(GL_COLOR_BUFFER_BIT);

        //glColorMask(true,true,true,true);
        glBindTexture(target, 0);
        glDisable(target);
        glColor3f(1,1,1);
        glBegin(GL_QUADS);


        glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);

        glVertex3f(polySizeX/2.0,polySizeY/2.0,1);

        glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);

        glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);

        glEnd();

        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO,GL_SRC_ALPHA);//
        //glBlendFunc(GL_ZERO,GL_SRC_COLOR);//
    }
}

void gfcPlate::stopAlphaBackround() {
    if (aMask) {
        glPopAttrib();
        glDisable(GL_BLEND);
    }
}

/**
 * Sets the frame and sequence we will use in the rest of the drawing functions. Also initializes other factors like the label to display on the plate and if the AOI overlay should be shown and writes the appropiate label that will be drawn.
 */
void gfcPlate::getFrameAndSequence() {

    std::ostringstream stream(std::iostream::out);
    stream.str("");
    theSequence=trackManager.getSequence(track);



    if (showPreview) {
		
        theFrame=theSequence->getPreviewFrame();
        aoi=theSequence->getAOI();
		if (drawingChoice==2)
		{
			return;
		}
		
        if (theFrame.loaded) {
            stream<< "---------------------------------------------"<<std::endl;
            stream<< "Sequence Information for Track " << (char)('A'+track)<<std::endl;
            stream<< "---------------------------------------------"<<std::endl;
            stream<< "Filename: "<<theFrame.fileName<<std::endl;
            stream<< "Resolution: " << theFrame.quadSizeX << " x "<<theFrame.quadSizeY<<std::endl;
            stream<< "Format: " << theFrame.format << "("<<theFrame.formatDescription<<")"<<std::endl;
            stream<< "Compression: "<<theFrame.compressionDescription<<std::endl;
            stream<< "Channels: "<<theFrame.originalChannels<<" ("<<theFrame.originalBitDepth<<" bits per channel)"<<std::endl;
            stream<< "Total Frames in Sequence: " << theSequence->getNumPreviewFrames()<<std::endl;
            stream<< "---------------------------------------------"<<std::endl<<std::endl;

            stream<< "---------------------------------------------"<<std::endl;
            stream<< "Selected Frame Information"<<std::endl;
            stream<< "---------------------------------------------"<<std::endl;
            stream<< "KeyKode: "<<theFrame.getMetadataItem("KeyKode")<<std::endl;
            stream<< "SMPTE TimeCode: "<<theFrame.getMetadataItem("SMPTE TimeCode")<<std::endl;
            stream<< "---------------------------------------------"<<std::endl<<std::endl;

            stream<< "---------------------------------------------"<<std::endl;
            stream<< "Loading Summary"<<std::endl;
            stream<< "---------------------------------------------"<<std::endl;
            int tmpNumToLoad=theSequence->myGUI->getTo()-theSequence->myGUI->getFrom()+1;
            stream<< "Total Frames to Load: " << (tmpNumToLoad) <<"("<<theSequence->myGUI->getFrom()<<"->"<<theSequence->myGUI->getTo()<< ")"<<std::endl;
            gfcRectang labelAOI=theSequence->getAOI();
            if (labelAOI.w!=-1 || labelAOI.h!=-1)stream<< "Crop to: " << labelAOI.w <<" x "<< labelAOI.h<<std::endl;
            switch (theFrame.compressed) {
            case GFC_8BPC:
                stream<< "Depth: 8 bit per component"  <<std::endl;
                break;

            case GFC_16HALF:
                stream<< "Depth: 16 bit (Half Floating Point) per component"  <<std::endl;
                break;

            case GFC_16BPC:
                stream<< "Depth: 16 bit per component"  <<std::endl;
                break;

            case GFC_4BPC:
                stream<< "Depth: 4bits per component"  <<std::endl;
                break;

            case GFC_S3TCDX1:
                stream<< "Depth: Compressed S3TC image"  <<std::endl;
                break;
            }
            stream<< "Scaled: "<<theFrame.scale<<"%"<<std::endl;
            stream<< "---------------------------------------------"<<std::endl;

        } else {
            stream<< "Select a Sequence to load into Track "<<(char)('A'+track) << " or drag and drop an image file onto the window"<<std::endl;


        }



    } else {
        aoi.set(-1,-1,-1,-1);

        theFrame=theSequence->getFrame(currentFrame,forRender);
        //pass the forRender parameter to force loads if necessary
		if (drawingChoice==2)
		{
			return;
		}

        switch (textMode) {
        case 0: //no text
            stream.str("");
            break;
        case 1:
            stream<<"Track "<< (char)('A'+track);
            stream<<"   f: "<<std::setw(8)<<std::setfill('0')<< currentFrame<<std::setw(0);
            stream<<"   t: "<<playbackManager.getTimecodeString();
            stream<<"   fps: "<< playbackManager.getCurrentFPS()<<std::endl;
            stream<<theFrame.getInfoString();
            //sprintf(label,"track %c  f: %08i    t: %s    fps:%.2f\n %s", this->track+'A',currentFrame,mw.vp->updateTimecode().c_str(),gFPS,theFrame.getInfoString().c_str());
            break;
        case 2:
            stream<<"Track "<< (char)('A'+track);
            stream<<"   f: "<<std::setw(8)<<std::setfill('0')<< currentFrame<<std::setw(0);
            stream<<"   t: "<<playbackManager.getTimecodeString();
            stream<<"   fps: "<< playbackManager.getCurrentFPS()<<std::endl;
            stream<<theFrame.getInfoString()<<std::endl<<std::endl;
            stream<<theFrame.getExtendedInfoString();
            break;
        default:

            break;
        }


    }
    labelString=stream.str();
}

void gfcPlate::storePointerInfo(gfcNetRemotePointerInfo info) {
    pointerStorage.store(info);

}

void gfcPlate::removePointerInfo(gfcNetRemotePointerInfo info) {
    pointerStorage.removeFromMap(info);
}

Vec3D gfcPlate::getCursorPositionIn2DSpace(int px, int py) {
    GLdouble projection[16],modelView[16];
    GLint viewport[4];

    Vec3D result;
    //the cursor position only makes sense when we are transformed and viewported
    //getFrameAndSequence();



    startViewport();
    startTransform();
    glViewport ( this->viewport.x,this->viewport.y,this->viewport.w,this->viewport.h );
    {
        glGetIntegerv( GL_VIEWPORT,viewport );
        glGetDoublev( GL_MODELVIEW_MATRIX,modelView );
        glGetDoublev( GL_PROJECTION_MATRIX,projection );

        gluUnProject ( px, py, -1, modelView, projection, viewport, &result.x, &result.y, &result.z);

        //printf ( "%i: pos ( %i,%i ) : %f,%f,%f\n",this->quadID,prevWinCoordX,prevWinCoordY,result.x,result.y,result.z);
    }
    endTransform();
    endViewport();

    if (theFrame.loaded) {
        result.z=theFrame.scale;
    } else {
        result.z=1;
    }

    return result;
}

void gfcPlate::setViewport(int x, int y, int w, int h) {
    viewport.set(x,y,w,h);
    histogramWindow.viewport=viewport;
}

void gfcPlate::startViewport() {
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(viewport.x,viewport.y,viewport.w,viewport.h);
}

void gfcPlate::endViewport() {
    glPopAttrib();
}




gfcNetTransformationInfo gfcPlate::getTransformations() {
    gfcNetTransformationInfo tmp;
    tmp.tY=tY;
    tmp.tX=tX;
    tmp.rZ=rZ;
    tmp.scale=scale;
    return tmp;
}

void gfcPlate::setTransformations(gfcNetTransformationInfo info) {
    tY=info.tY;
    tX=info.tX;
    rZ=info.rZ;
    scale=info.scale;


    myGUI->setScale(scale);
    myGUI->setTX(tX);
    myGUI->setTY(tY);
    myGUI->setRZ(rZ);

}

gfcNetPlateStateInfo gfcPlate::getPlateStateInfo() {
    gfcNetPlateStateInfo result;
    result.a=aMask;
    result.r=rMask;
    result.g=gMask;
    result.b=bMask;

	//printf("Getting track plateStateInfo: %c\n",track);
    result.track=track;
    result.flip=flip;
    result.flop=flop;
    result.crop=cropOn;
    result.aspect=myGUI->getAspectString();
    result.quadID=quadID;
    return result;
}

void gfcPlate::setPlateStateInfo(gfcNetPlateStateInfo info) {

    aMask=info.a;
    rMask=info.r;
    gMask=info.g;
    bMask=info.b;

    track=info.track;
    flip=info.flip;
    flop=info.flop;
    cropOn=info.crop;
    myGUI->setAspectChoice(info.aspect);
    aspect=myGUI->getAspect();
		
    myGUI->setChannelR(rMask);
    myGUI->setChannelG(gMask);
    myGUI->setChannelB(bMask);
    myGUI->setChannelA(aMask);
	this->setRGBAGUIFromCurrentMasks();
    myGUI->setFlip(flip);
    myGUI->setFlop(flop);
    myGUI->setTrackChoice(track);
    myGUI->setCrop(cropOn);
	

}

gfcNetPlateColorCorrectionInfo gfcPlate::getPlateColorCorrectionInfo()
{
	gfcNetPlateColorCorrectionInfo result;
	result.lutName=myGUI->getLUTName();
	result.gamma=gamma;
	result.exposure=exposure;
	result.brightness=brightness;
	result.contrast=contrast;
	result.saturation=saturation;
	result.quadID=quadID;
	return result;
}
void gfcPlate::setPlateColorCorrectionInfo(gfcNetPlateColorCorrectionInfo info)
{
	gfcNetPlateColorCorrectionInfo prevColor=getPlateColorCorrectionInfo();

	int lutIndex=lutManager.getLutIndexByName(info.lutName);
	setLUT(lutIndex);
	gamma=info.gamma;
	exposure=info.exposure;
	brightness=info.brightness;
	contrast=info.contrast;
	saturation=info.saturation;

	myGUI->setLUT(lutIndex+1); 
	myGUI->setGamma(gamma);
	myGUI->setExposure(exposure);
	myGUI->setBrightness(brightness);
	myGUI->setContrast(contrast);
	myGUI->setSaturation(saturation);

	if (prevColor.gamma!=info.gamma) plateManager.setFeedbackMessage(std::string("gamma: ")+ftos(gamma,2));
	if (prevColor.exposure!=info.exposure) plateManager.setFeedbackMessage(std::string("exposure: ")+ftos(exposure,2));
	if (prevColor.brightness!=info.brightness) plateManager.setFeedbackMessage(std::string("brightness: ")+ftos(brightness,2));
	if (prevColor.contrast!=info.contrast) plateManager.setFeedbackMessage(std::string("contrast: ")+ftos(contrast,2));
	if (prevColor.saturation!=info.saturation) plateManager.setFeedbackMessage(std::string("saturation: ")+ftos(saturation,2));
	 
	//printf("prevColor.lutName: %s\ninfo.lutName:%s\n\n",prevColor.lutName.c_str(),info.lutName.c_str());
	if (prevColor.lutName!=info.lutName) plateManager.setFeedbackMessage(std::string("LUT: ")+info.lutName);
	
}

void gfcPlate::processNetFXAttribInfo(gfcNetFXAttribInfo &info) {
    fxStack.processNetFXAttribInfo(info);
}

void gfcPlate::processNetFXCommonInfo(gfcNetFXCommonInfo &info) {
    fxStack.processNetFXCommonInfo(info);
}

void gfcPlate::setRemotePointerOptions(int pfontSize, int psize, bool pfade, int pfadeDelay, bool ptrail, float ptrailLenght,int pColor) {
    pointerStorage.maxPointerStore=ptrail?ptrailLenght:0;

    if (pfade)
        pointerStorage.fadeDelay=1.0/pfadeDelay;
    else
        pointerStorage.fadeDelay=0;

    remotePointerColor=pColor;
    remotePointerSize=psize;
    remotePointerFontSize=pfontSize;


}

void gfcPlate::savePlateSessionParameters(XMLNode & plateNode) {
    saveSetting("trackID",(int)this->track,plateNode);
    saveSetting("tX",this->tX,plateNode);
    saveSetting("tY",this->tY,plateNode);
	saveSetting("rZ",this->rZ,plateNode);
    saveSetting("scale",this->scale,plateNode);
    saveSetting("flip",(int)this->flip,plateNode);
    saveSetting("flop",(int)this->flop,plateNode);
    saveSetting("crop",(int)this->cropOn,plateNode);
    saveSetting("aspect",this->myGUI->getAspectString(),plateNode);
    saveSetting("r",(int)this->rMask,plateNode);
    saveSetting("g",(int)this->gMask,plateNode);
    saveSetting("b",(int)this->bMask,plateNode);
    saveSetting("a",(int)this->aMask,plateNode);
	saveSetting("gamma",(float)this->gamma,plateNode);
	saveSetting("exposure",(float)this->exposure,plateNode);
	saveSetting("brightness",(float)this->brightness,plateNode);
	saveSetting("contrast",(float)this->contrast,plateNode);
	saveSetting("saturation",(float)this->saturation,plateNode);
	saveSetting("lut",this->myGUI->getLUTName(),plateNode);

    XMLNode stackNode=plateNode.addChild("stack");
    fxStack.saveStackToNode(stackNode);

}

void gfcPlate::loadPlateSessionParameters(XMLNode & plateNode) {
    setTrack(readAttributeFromNode<int>("trackID",plateNode,0));
    setZoom(readAttributeFromNode<float>("scale",plateNode,1.0));
    setTX(readAttributeFromNode<int>("tX",plateNode,0));
    setTY(readAttributeFromNode<int>("tY",plateNode,0));
	setRZ(readAttributeFromNode<int>("rZ",plateNode,0));
    setFlip(readAttributeFromNode<int>("flip",plateNode,0));
    setFlop(readAttributeFromNode<int>("flop",plateNode,0));
    setCrop(readAttributeFromNode<int>("crop",plateNode,0));
    setAspect(readAttributeFromNode<std::string>("aspect",plateNode,"original"));
    setChannelR(readAttributeFromNode<int>("r",plateNode,1));
    setChannelG(readAttributeFromNode<int>("g",plateNode,1));
    setChannelB(readAttributeFromNode<int>("b",plateNode,1));
    setChannelA(readAttributeFromNode<int>("a",plateNode,1));
	setGamma(readAttributeFromNode<float>("gamma",plateNode,1.0));
	setExposure(readAttributeFromNode<float>("exposure",plateNode,0.0));
	setBrightness(readAttributeFromNode<float>("brightness",plateNode,1.0));
	setContrast(readAttributeFromNode<float>("contrast",plateNode,1.0));
	setSaturation(readAttributeFromNode<float>("saturation",plateNode,1.0));
	this->setRGBAGUIFromCurrentMasks();
	setLUT(lutManager.getLutIndexByName(readAttributeFromNode<std::string>("lut",plateNode,"no LUT")));
   


}

void gfcPlate::setTrack(int value) {
    clearHistogramCache();
    this->track=value;
    myGUI->setTrackChoice(value);
	char theTrack[2];
	theTrack[0]=track+'A';
	theTrack[1]=0;
	plateManager.setFeedbackMessage(std::string("Changed to Track: ")+theTrack);
}

void gfcPlate::setZoom(float value) {
    this->scale=value;
    myGUI->setScale(scale);
	
}

void gfcPlate::setTX(int value) {
    this->tX=value;
    myGUI->setTX(tX);
}

void gfcPlate::setTY(int value) {
    tY=value;
    myGUI->setTY(value);
}

void gfcPlate::setRZ(float value) {
	this->rZ=value;
	myGUI->setRZ(value);
}

void gfcPlate::setGamma(float value,int relative){
	if (relative)
	{
		gamma+=value;
	}
	else
	{
		gamma=value;
	}
		
	gamma=max(gamma,0); //clip to 0
	myGUI->setGamma(gamma);
	plateManager.setFeedbackMessage(std::string("gamma:")+ftos(gamma,3));
}

void gfcPlate::setExposure(float value,int relative)
{
	if (relative)
	{
		exposure+=value;
	}
	else
	{
		exposure=value;
	}
	myGUI->setExposure(exposure);
	plateManager.setFeedbackMessage(std::string("exposure:")+ftos(exposure,2));
}
void gfcPlate::setBrightness(float value,int relative)
{
	if (relative)
	{
		brightness+=value;
	}
	else
	{
		brightness=value;
	}

	brightness=max(brightness,0); //clip to 0
	myGUI->setBrightness(brightness);
	plateManager.setFeedbackMessage(std::string("brightness:")+ftos(brightness,2));
}
void gfcPlate::setContrast(float value,int relative)
{
	if (relative)
	{
		contrast+=value;
	}
	else
	{
		contrast=value;
	}
	contrast=max(contrast,0); //clip to 0
	myGUI->setContrast(contrast);
	plateManager.setFeedbackMessage(std::string("contrast:")+ftos(contrast,2));
}
void gfcPlate::setSaturation(float value,int relative)
{
	if (relative)
	{
		saturation+=value;
	}
	else
	{
		saturation=value;
	}
	saturation=max(saturation,0); //clip to 0
	myGUI->setSaturation(saturation);
	plateManager.setFeedbackMessage(std::string("saturation:")+ftos(saturation,2));
}

void gfcPlate::scrollLUT(int direction)
{
	int totalLUTS=lutManager.getAllNames().size();
	int currentLUTIndex=myGUI->getLUT();
	int tmpLUTIndex=currentLUTIndex+direction;
	if (tmpLUTIndex<0)
	{
		tmpLUTIndex=0;
	}
	else
	{
		if (tmpLUTIndex>totalLUTS)
		{
			tmpLUTIndex=totalLUTS;
		} 
	}

	if (currentLUTIndex!=tmpLUTIndex)
	{
		setLUT(tmpLUTIndex-1);
	}
	//if ()
	{
		plateManager.setFeedbackMessage(std::string("LUT: ")+lutManager.getLUT(tmpLUTIndex-1).getNameNoPath());
	}
	
}

void gfcPlate::setLUT(int lutIndex)
{
	CubeLUT tmpLut=lutManager.getLUT(lutIndex);
	lutID=tmpLut.getTextureID();
	lutType=tmpLut.type;
	lutSize=tmpLut.size;

	myGUI->setLUT(lutIndex+1);
}

void gfcPlate::setFlip(int value) {
    flip=value;
    myGUI->setFlip(flip);
	plateManager.setFeedbackMessage(std::string("flip: ") + (flip?"on":"off"));
}

void gfcPlate::setFlop(int value) {
    flop=value;
    myGUI->setFlop(flop);
	plateManager.setFeedbackMessage(std::string("flop: ")+ (flop?"on":"off"));
}

void gfcPlate::toggleFlip()
{
	setFlip(!flip);
}

void gfcPlate::toggleFlop()
{
	setFlop(!flop);
}



void gfcPlate::setCrop(int value) {
    this->cropOn=value;
    myGUI->setCrop(cropOn);
}

void gfcPlate::setAspect(std::string value) {
    myGUI->setAspectChoice(value);
    aspect=myGUI->getAspect();
	plateManager.setFeedbackMessage(std::string("aspect:")+value);
}

int gfcPlate::getActiveFromGUI()
{
	return myGUI->getActive();

}

void gfcPlate::setRGBAGUIFromCurrentMasks()
{
	if (rMask && gMask && bMask && !aMask )
	{
		myGUI->setRGBA(0);
	}
		
}

void gfcPlate::setChannelR(int value) {
    rMask=value;
    myGUI->setChannelR(rMask);
	
	//setRGBAGUIFromCurrentMasks();
}

void gfcPlate::setChannelG(int value) {

    gMask=value;
    myGUI->setChannelG(gMask);
	
	//setRGBAGUIFromCurrentMasks();
}

void gfcPlate::setChannelB(int value) {
    bMask=value;
    myGUI->setChannelB(bMask);
	
	//setRGBAGUIFromCurrentMasks();
}

void gfcPlate::setChannelA(int value) {

    aMask=value;
    myGUI->setChannelA(aMask);
	
	//setRGBAGUIFromCurrentMasks();
}

void gfcPlate::toggleChannelR() {
	
	if (rMask && !bMask && !gMask && !aMask)
	{
		//turn on all
		setChannelR(1);
		setChannelG(1);
		setChannelB(1);
		setChannelA(0);
		plateManager.setFeedbackMessage(std::string("Channel Mask: RGB"));
	}
	else
	{
		//turn all off except red
		setChannelR(1);
		setChannelG(0);
		setChannelB(0);
		setChannelA(0);
		plateManager.setFeedbackMessage(std::string("Channel Mask: R"));
	}
	
	setRGBAGUIFromCurrentMasks();
	
}

void gfcPlate::toggleChannelG() {
	if (!rMask && !bMask && gMask && !aMask)
	{
		//turn on all
		setChannelR(1);
		setChannelG(1);
		setChannelB(1);
		setChannelA(0);	
		plateManager.setFeedbackMessage(std::string("Channel Mask: RGB"));
	}
	else
	{
		//turn all off except red
		setChannelR(0);
		setChannelG(1);
		setChannelB(0);
		setChannelA(0);
		plateManager.setFeedbackMessage(std::string("Channel Mask: G"));
	}	

	setRGBAGUIFromCurrentMasks();
}

void gfcPlate::toggleChannelB() {
	if (!rMask && bMask && !gMask && !aMask)
	{
		//turn on all
		setChannelR(1);
		setChannelG(1);
		setChannelB(1);
		setChannelA(0);
		plateManager.setFeedbackMessage(std::string("Channel Mask: RGB"));
	}
	else
	{
		//turn all off except red
		setChannelR(0);
		setChannelG(0);
		setChannelB(1);
		setChannelA(0);
		plateManager.setFeedbackMessage(std::string("Channel Mask: B"));
	}

	setRGBAGUIFromCurrentMasks();
}

void gfcPlate::toggleChannelA() {
	if (!rMask && !bMask && !gMask && aMask)
	{
		//turn on all
		setChannelR(1);
		setChannelG(1);
		setChannelB(1);
		setChannelA(0);	
		plateManager.setFeedbackMessage(std::string("Channel Mask: RGB"));
	}
	else
	{
		//turn all off except red
		setChannelR(0);
		setChannelG(0);
		setChannelB(0);
		setChannelA(1);
		plateManager.setFeedbackMessage(std::string("Channel Mask: A"));
	}

	setRGBAGUIFromCurrentMasks();
}

void gfcPlate::fitToViewport(){
	

	//for debbugging purposes
	//printf("Use Shader: %i\n",useShader);
	

	//center the frame
	resetTransforms();
	
	if (!theFrame.loaded || theFrame.quadSizeX<=0.0 || theFrame.quadSizeY<=0.0)
	{
		this->setZoom(1.0);
		return;
	}
	

	//figure out the best scale
	getFrameAndSequence();
	float sX=theFrame.quadSizeX;
	float sY=theFrame.quadSizeY;
	
	float vpW=viewport.w;
	float vpH=viewport.h;
	
	float ratioX=vpW/sX;
	float ratioY=vpH/sY;
	
	//printf("vp:%f %f\nquadSize: %f %f\nratios: %f %f\n",vpW,vpH,sX,sY,ratioX, ratioY);
	
	
	//printf("ratioX %f, ratioY %f\n",ratioX, ratioY);
	if (ratioX>ratioY)
	{
		if (ratioY<=1.0 && ratioY>0)
		{	
			this->setZoom(ratioY);
		}
		
	}
	else
	{
		if (ratioX<=1.0 && ratioX>0)
		{
			this->setZoom(ratioX);
		}
		
	}

	plateManager.setFeedbackMessage("Fit plate to viewport");
}


void gfcPlate::setTextDisplayOptions(int pfontSize, float pcolor, float popacity) {
    this->textDisplaySize=pfontSize;
    this->textDisplayColor=pcolor;
    this->textDisplayOpacity=popacity;
}

void gfcPlate::clearHistogramCache() {
    histogramCache.clear();
}

void gfcPlate::setHistogramQuality(int pQuality) {

    histogramQuality=pQuality;
    clearHistogramCache();
}

void gfcPlate::drawForSelection(int pcurrentFrame) {
    if (!myGUI)
        return;

    currentFrame=pcurrentFrame;
    target=GL_TEXTURE_RECTANGLE_ARB;
    areaOfIntrestOn=false;
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_TEXTURE_2D);

    //************GET THE GFCFRAME AND WRITE THE LABEL
    {
        getFrameAndSequence();
    }
    //END OF GET THE GFCFRAME AND WRITE THE LABEL


    //********set the viewport for this plate************
    startViewport();

    if (theFrame.loaded) {
        //*********CALCULATE POLY SIZES, CROP BARS ETC***********
        calculatePolySizesCropEtc();
        //*********TRANSFORM WHATHEVER WE WILL DRAW**********
        startTransform();

        {
            //draw the textures rectangle, without the texture of course, just a unique color.
			glColor3ubv((GLubyte*)framePick.getPickColor().colors);


            //printf("texCoords: %f %f %f %f\n",texCoords.x,texCoords.y,texCoords.w,texCoords.h);
            glBegin(GL_QUADS);


            glVertex3f(-polySizeX/2.0,polySizeY/2.0,1);


            glVertex3f(polySizeX/2.0,polySizeY/2.0,1);


            glVertex3f(polySizeX/2.0,-polySizeY/2.0,1);

            glVertex3f(-polySizeX/2.0,-polySizeY/2.0,1);

            glEnd();

        }
        //END OF DRAW THE POLY



        drawAOIOverlay();
        //**********RESTORE THE MODELVIEW MATRIX******
        endTransform();


    }

    //**********DRAW THE HISTOGRAM AND VECTORSCOPE*****************
    drawHistogram();
    drawVectorscope();

    //********restore the viewport************
    endViewport();


}

void gfcPlate::setRenderModeSelection(int value) {
    renderModeSelection=value;
}

int gfcPlate::pickNotify(gfcPickNotifyParameters & params) {

	int somethingClicked = 0;

    /*printf("Plate got Click event: %i, at %ix%i (%i, %i), color: %i %i %i\n",params.event, \
    			params.x, params.y, \
    			params.dx, params.dy, \
    			params.pickedColor.colors[0], params.pickedColor.colors[1], params.pickedColor.colors[2]);*/

	//see if the histogram was hit.
	//printf("Histogram Picking for quad %i\n",quadID);
	somethingClicked |=	histogramWindow.pickNotify(params);
	
	
	
	//see if the AOI points where hit and process
	gfcSequence *tmpSeq = trackManager.getSequence(this->quadID);
	
	//NW corner affects h and x
	gfcPickObjectStatus status=aoiPickNW.getStatus(params);
	if (status.clicked)
	{
		//printf("NW Clicked (dx:%i dy:%i)\n",status.dx, status.dy);
		tmpSeq->setAOI(-status.dx/(float)scale,0,status.dx/scale,status.dy/(float)scale,true);	
	}
	somethingClicked |= status.clicked;
	

	//SW corner affects x and y
	status=aoiPickSW.getStatus(params);
	if (status.clicked)
	{
		//printf("SW Clicked (dx:%i dy:%i)\n",status.dx, status.dy);
		tmpSeq->setAOI(-status.dx/(float)scale,status.dy/(float)scale,status.dx/(float)scale,-status.dy/(float)scale,true);	
		somethingClicked = 1;
	}
	

	//SE corner affects w and y
	status=aoiPickSE.getStatus(params);
	if (status.clicked)
	{
		//printf("SE Clicked (dx:%i dy:%i)\n",status.dx, status.dy);
		tmpSeq->setAOI(0,status.dy/(float)scale,-status.dx/(float)scale,-status.dy/(float)scale,true);	
		somethingClicked = 1;
	}
	
	//NE corner affects w and h
	status=aoiPickNE.getStatus(params);
	if (status.clicked)
	{
		//printf("NE Clicked (dx:%i dy:%i)\n",status.dx, status.dy);
		tmpSeq->setAOI(0,0,-status.dx/(float)scale,status.dy/(float)scale,true);
		somethingClicked = 1;
	}
	
	//they clicked on the AOI to move it
	status=aoiPickMove.getStatus(params);
	if (status.clicked && !(status.flags & GFC_PICK_MODIFIER_CTRL))
	{
		//printf("Move Clicked (dx:%i dy:%i)\n",status.dx, status.dy);
		tmpSeq->setAOI(-status.dx/float(scale),status.dy/(float)scale,0,0,true);
		somethingClicked = 1; //this will prevent ctrl drags from moving the aoi.

	}

	
	return somethingClicked;

}


