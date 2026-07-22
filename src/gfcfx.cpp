#include <glad/glad.h>
#include "gfcfx.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include <fstream>
#include "trilerp.h"
#include "xmlParser.h"
#include "gfcStructures.h"

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

//#define PRINTFXLOADINGMESSAGES

#define FX_GUI_WIDGET_SIZE_X 35
#define FX_GUI_WIDGET_SIZE_Y 20
#define FX_GUI_LABEL_SPACE 25
//20 for the actual widget and 15 more for the label on top.


//extern std::vector<CubeLUT> lutArray;

std::string getInfoLog ( GLhandleARB obj )
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
		free ( infoLog );
	}
	return returnValue;
}

int getShaderCompileStatus ( GLhandleARB obj )
{
	int status = 0;
	
	glGetObjectParameterivARB ( obj, GL_OBJECT_COMPILE_STATUS_ARB,
	                            ( GLint* ) &status);

	return status;
}

std::string gfcFX::getNameNoPath()
{

	int pos=filename.find_last_of("/\\'");
	if(pos!=std::string::npos)
	return filename.substr(pos+1);
	else
	return filename;
}

gfcFX::gfcFX()
{
	active=true; //Fxs are on by default
	floatsNum=0;
	numOfTextures=0;
	cubesNum=0;
	boolsNum=0;
	sizeX=sizeY=0;
	loadedAndCompiled=false;
	autoload=true;
	vertexShader=0;
	fragmentShader=0;
	ShaderProgram=0;
}

/*gfcFX::gfcFX(const gfcFX &fx)
{
	printf("Copy constructor called on %s\n",fx.name.c_str());
	name = "pinga loca";
	/*active=fx.active;
	author=fx.author;
	name=fx.name;
	version=fx.version;

	floats=fx.floats;
	floatsNum=fx.floatsNum;

	textureNum=fx.textureNum;
	textures=fx.textures;

	cubes=fx.cubes;
	cubesNum=fx.cubesNum;

	bools=fx.bools;
	boolsNum=fx.boolsNum;

	filename=fx.filename;
	autoload=fx.autoload;
	loadedAndCompiled=fx.loadedAndCompiled;
	errorWhileLoading=fx.errorWhileLoading;

	fragmentShader=fx.fragmentShader;
	vertexShader=fx.vertexShader;
	ShaderProgram=fx.ShaderProgram;

	groups=fx.groups;
	sizeX=fx.sizeX;
	sizeY=fx.sizeY;
	md5Hash=fx.md5Hash;
	guiOpen=false;
	printf("asignee: %s\n",name.c_str());
}*/

/*gfcFX::~gfcFX()
{}*/

/*gfcFX& gfcFX::operator=( const gfcFX &fx )
{
	printf("Assignment called on %s ",fx.name.c_str());
	active=fx.active;
	author=fx.author;
	name=fx.name;
	version=fx.version;

	floats=fx.floats;
	floatsNum=fx.floatsNum;

	textureNum=fx.textureNum;
	textures=fx.textures;

	cubes=fx.cubes;
	cubesNum=fx.cubesNum;

	bools=fx.bools;
	boolsNum=fx.boolsNum;

	filename=fx.filename;
	autoload=fx.autoload;
	loadedAndCompiled=fx.loadedAndCompiled;
	errorWhileLoading=fx.errorWhileLoading;

	fragmentShader=fx.fragmentShader;
	vertexShader=fx.vertexShader;
	ShaderProgram=fx.ShaderProgram;

	groups=fx.groups;
	sizeX=fx.sizeX;
	sizeY=fx.sizeY;
	md5Hash=fx.md5Hash;
	guiOpen=false;
	printf("asignee: %s\n",name.c_str());
	return *this;
}*/

/**
 * Loads a JFX plugin file from pfilename
 * @param pfilename
 */
int gfcFX::load ( const std::string pfilename, int type, Fl_Progress *pprogress )
{
	
	loadedAndCompiled=false;
	errorWhileLoading=false;
	//char shaderPath[2500];
	std::string shaderPath;
	//printf("*LOADING FX PLUGIN: %s",GetFilenameNoPath(pfilename).c_str());
	//open and parse the XML plugin file
	std::string inputForHash;
	
	if(!sett.glsl)
	{
		if ( pprogress!=NULL )
		{
		pprogress->label ( "FXs not Supported" );
		pprogress->value ( 0 );
		pprogress->selection_color ( fl_rgb_color(42,20,20));
		app().processEvents();
		}
		return 1;
		
	}
	
	if ( pprogress!=NULL )
	{
		pprogress->label ( "Loading File" );
		pprogress->value ( 0 );
		pprogress->selection_color ( fl_rgb_color(42,42,20));
		app().processEvents();
	}

	/*switch(type)
	{
		case 0: //from filename
		{
		
		}
		break;
	}*/

	XMLNode xMainNode;
	xMainNode=XMLNode::openFileHelper ( pfilename.c_str() );
	//printf ( "xMainNodeLoaded=%i,",xMainNode.loaded );
	//printf("Noma\n");
	if ( xMainNode.loaded==false )
	{
		printf ( "Error opening plugin File\n" );
		if ( pprogress!=NULL )
		{
			pprogress->label ( "Error opening File!" );
			pprogress->value ( 0 );
			pprogress->selection_color (  fl_rgb_color(42,20,20) );
			app().processEvents();
		}
		return 1;
	}

	//obtain general info
	XMLNode xNode=xMainNode.getChildNode ( "root" ).getChildNode ( "general" );
	//Load general info
	{
		filename =  pfilename ;

		author =  xNode.getAttribute ( "author" ) ;
#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "author: %s\n",author );
#endif

		name =  xNode.getAttribute ( "name" ) ;
#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "name: %s\n",name );
#endif

		version =  xNode.getAttribute ( "version" ) ;
#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "version: %s\n",version );
#endif

		description =  xNode.getAttribute ( "description" ) ;
#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "description: %s\n",description );
#endif

		menuName =  xNode.getAttribute ( "menuName" ) ;
#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "menuName: %s\n",menuName );
#endif
		//inputForHash=filename;
		inputForHash+=author;
		inputForHash+=name;
		inputForHash+=version;
		inputForHash+=description;
		inputForHash+=menuName;
	}
	//end of general info

	//Load widget groups
	{
		xNode=xMainNode.getChildNode ( "root" ).getChildNode ( "groups" );
		int numOfGroups=xNode.nChildNode ( "group" );
		int xmlGroupIter=0;
#ifdef PRINTFXLOADINGMESSAGES

		printf ( "No. of Groups: %i\n",numOfGroups );
#endif
		//iterate each group
		for ( int i=0; i<numOfGroups;i++ )
		{
			gfcFXWidgetGroup group;
			XMLNode groupNode=xNode.getChildNode ( "group",&xmlGroupIter );
			group.name= groupNode.getAttribute ( "name" ) ;
#ifdef PRINTFXLOADINGMESSAGES

			printf ( "Group %i:\n -Name: %s\n -Widgets:\n",i,group.name.c_str() );
#endif

			//for each group, iterate it's widgets.
			int groupSizeXmax=0;
			int groupSizeXtmp=0;
			int groupSizeY=0;

			int numOfWidgets=groupNode.nChildNode ( "widget" );
			int xmlWidgetIter=0;
			for ( int j=0;j<numOfWidgets;j++ )
			{
				XMLNode widgetNode=groupNode.getChildNode ( "widget",&xmlWidgetIter );
				gfcFXWidget widget;

				if ( strcmp ( widgetNode.getAttribute ( "type" ),"float" ) ==0 )
					widget.type=FX_GUI_FLOAT;
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"bool" ) ==0 )
					widget.type=FX_GUI_BOOL;
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"texture" ) ==0 )
				{
					widget.type=FX_GUI_TEXTURE;
					numOfTextures++;
				}
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"cube" ) ==0 )
					widget.type=FX_GUI_CUBE;
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"lut" ) ==0 )
					widget.type=FX_GUI_LUT;
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"newLine" ) ==0 )
					widget.type=FX_GUI_NEWLINE;
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"choice" ) ==0 )
				{
					widget.type=FX_GUI_CHOICE;
					//if it's a choice widget, read all the options for that choice.
					int numOfOptions=widgetNode.nChildNode ( "choice" );
					int optionsIterator=0;
					for ( int k=0;k<numOfOptions;k++ )
					{
						widget.options.push_back ( (widgetNode.getChildNode ( "choice",&optionsIterator ).getAttribute ( "label" ))  );
					}
				}
				else if ( strcmp ( widgetNode.getAttribute ( "type" ),"spacer" ) ==0 )
					widget.type=FX_GUI_SPACER;
				else
					widget.type=FX_GUI_UNKNOWN;

				widget.varName= widgetNode.getAttribute ( "varName" )?widgetNode.getAttribute ( "varName" ):"";
				widget.label= widgetNode.getAttribute ( "label" )?widgetNode.getAttribute ( "label" ):"";

				if ( widgetNode.getAttribute ( "labelColorR" ) !=NULL )
					widget.labelColor.x=atof ( widgetNode.getAttribute ( "labelColorR" ) );
				if ( widgetNode.getAttribute ( "labelColorG" ) !=NULL )
					widget.labelColor.y=atof ( widgetNode.getAttribute ( "labelColorG" ) );
				if ( widgetNode.getAttribute ( "labelColorB" ) !=NULL )
					widget.labelColor.z=atof ( widgetNode.getAttribute ( "labelColorB" ) );

				if ( widgetNode.getAttribute ( "width" ) !=NULL )
					widget.width=atoi ( widgetNode.getAttribute ( "width" ) );

				if ( widgetNode.getAttribute ( "minimum" ) !=NULL )
					widget.minimum=atof ( widgetNode.getAttribute ( "minimum" ) );

				if ( widgetNode.getAttribute ( "maximum" ) !=NULL )
					widget.maximum=atof ( widgetNode.getAttribute ( "maximum" ) );

				if ( widgetNode.getAttribute ( "step" ) !=NULL )
					widget.step=atof ( widgetNode.getAttribute ( "step" ) );

				if ( widgetNode.getAttribute ( "default" ) !=NULL )
					widget.defaultValue=atof ( widgetNode.getAttribute ( "default" ) );

				widget.value=widget.defaultValue;

#ifdef PRINTFXLOADINGMESSAGES

// 				printf ( "  -Widget %i:\n\
// 				         -type: %i\n\
// 				         -varName: %s\n\
// 				         -label: %s\n\
// 				         -labelColor: %f,%f,%f\n\
// 				         -minimum: %f\n\
// 				         -maximum: %f\n\
// 				         -step: %f\n\
// 				         -defaultValue: %f\n",j,widget.type,widget.varName,widget.label,widget.labelColor.x,widget.labelColor.y,widget.labelColor.z, widget.minimum, widget.maximum, widget.step, widget.defaultValue );
#endif

				switch ( widget.type )
				{
					case FX_GUI_FLOAT:
					case FX_GUI_BOOL:
					case FX_GUI_CUBE:
					case FX_GUI_LUT:
					case FX_GUI_TEXTURE:

						groupSizeXtmp+=FX_GUI_WIDGET_SIZE_X;

						break;

					case FX_GUI_NEWLINE:

						if ( groupSizeXtmp>groupSizeXmax )
							groupSizeXmax=groupSizeXtmp;

						groupSizeXtmp=0;
						groupSizeY+=FX_GUI_WIDGET_SIZE_Y+FX_GUI_LABEL_SPACE;

						break;

				}
				
				
				group.widgets[widget.varName]=widget;
				group.widgetsOrder.push_back ( widget.varName );
				

			}
			group.sizeX=groupSizeXmax;
			group.sizeY=groupSizeY;
#ifdef PRINTFXLOADINGMESSAGES

			printf ( " -Group Size: %i, %i\n",group.sizeX,group.sizeY );
#endif

			sizeX+=group.sizeX+10; //add a little border to FX GUI.
			sizeY+=group.sizeY+10; //account for the space between groups and the fxs common controls.

			this->groups[group.name]=group;
		}



	}

#ifdef PRINTFXLOADINGMESSAGES
	printf ( "-FX GUI Size: %i, %i\n",sizeX,sizeY );
#endif

	//Load Shaders
	{
		xNode=xMainNode.getChildNode ( "root" ).getChildNode ( "shaders" );
		//Load Vertex Shader
		shaderPath=GetPathFromFilename(pfilename);
		

#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "Path to shaders: %s\n",shaderPath );
#endif

		vertex =  ( xNode.getAttribute ( "vertex" ) )?( xNode.getAttribute ( "vertex" ) ):"";
		fragment = ( xNode.getAttribute ( "fragment" ) )?( xNode.getAttribute ( "fragment" ) ):"";

#ifdef PRINTFXLOADINGMESSAGES

		//printf ( "Loading Vertex shader: %s\n",vertex );
#endif

		{
			char *vertexSource;
			std::ifstream vertexFile;
			
			std::string tmpShaderName=shaderPath+vertex;
			
			vertexFile.open ( tmpShaderName.c_str() );
			if ( !vertexFile.is_open() )
			{
				printf ( "Could not find vertex source %s!\n",tmpShaderName.c_str() );
				if ( pprogress!=NULL )
				{
					pprogress->label ( "Could not find vertex source" );
					pprogress->value ( pprogress->maximum() );
					pprogress->selection_color (  fl_rgb_color(42,20,20));
					app().processEvents();
				}
				return 1;
			}
			else
			{
				int length;
				int *lengthv=&length;
#ifdef PRINTFXLOADINGMESSAGES

				printf ( "Vertex Source opened!\n" );
#endif

				vertexFile.seekg ( 0, std::ios::end );
				length = vertexFile.tellg();
				vertexFile.seekg ( 0, std::ios::beg );
				vertexSource=new char[length];
				for(int i=0;i<length;i++)
					vertexSource[i]='\0';
				vertexFile.read ( vertexSource,length );
				// printf("\n***********\n%s\n***********\n",vertexSource);
				vertexShader=glCreateShaderObjectARB ( GL_VERTEX_SHADER_ARB );

				//shaderSource takes an array of strings, so create one even if we only have one string
				const char * vv = vertexSource; 
				glShaderSourceARB ( vertexShader, 1, &vv, ( GLint* ) lengthv );
				delete [] vertexSource;
				//inputForHash+=vertexSource; //to create the md5 hash

				//delete vertexSource; //release shader source memory;
#ifdef PRINTFXLOADINGMESSAGES

				printf ( "Compiling Vertex Shader...\n" );
#endif
				
				glCompileShaderARB ( vertexShader );
				
				
				int compileStatus=getShaderCompileStatus(vertexShader);
				

				if ( !compileStatus )
				{
					std::string infoLog;
					infoLog = getInfoLog ( vertexShader );
					printf ( "Vertex Shader compilation error!:\n%s\nFX not loaded!\n",infoLog.c_str() );
					compilationError="Vertex Shader Compilation Error: ";
					compilationError+=vertex;
					compilationError+=":\n\n";
					compilationError+=infoLog;
					//delete [] shaderPath;
					if ( pprogress!=NULL )
					{
						pprogress->label ( "Vertex Shader Error" );
						pprogress->value ( 0 );
						pprogress->selection_color (  fl_rgb_color(42,20,20) );
						app().processEvents();
					}
					errorWhileLoading=true;
					//return 1;
				}

				//if ( infoLog.size() >0 )
				//{
				//	printf ( "Vertex Shader compilation error!:\n%s\nFX not loaded!\n",infoLog.c_str() );
				//	compilationError="Vertex Shader Compilation Error: ";
				//	compilationError+=vertex;
				//	compilationError+=":\n\n";
				//	compilationError+=infoLog;
				//	//delete [] shaderPath;
				//	if ( pprogress!=NULL )
				//	{
				//		pprogress->label ( "Vertex Shader Error" );
				//		pprogress->value ( 0 );
				//		pprogress->selection_color (  fl_rgb_color(42,20,20) );
				//		app().processEvents();
				//	}
				//	errorWhileLoading=true;
				//	//return 1;
				//}
				//else
				//{
				//	//printf("..OK!\n");
				//}
			}
		}
		//Load Fragment Shader
		//Load fragment Shader
#ifdef PRINTFXLOADINGMESSAGES
		printf ( "Loading fragment shader: %s\n",fragment );
#endif

		{
			//char *fragmentSource;
			std::ifstream fragmentFile;

			std::string tmpShaderName=shaderPath+fragment;

			fragmentFile.open ( tmpShaderName.c_str() );
			if ( !fragmentFile.is_open() )
			{
				printf ( "Could not find fragment source %s!\n",tmpShaderName.c_str() );
				if ( pprogress!=NULL )
				{
					pprogress->label ( "Could not find fragment source" );
					pprogress->value ( 0 );
					pprogress->selection_color (  fl_rgb_color(42,20,20) );
					app().processEvents();
				}
				errorWhileLoading=true;
				//return 1;
			}
			else
			{
				int length;
				int *lengthv=&length;
#ifdef PRINTFXLOADINGMESSAGES

				printf ( "fragment Source opened!\n" );
#endif
				char *fragmentSource;
				fragmentFile.seekg ( 0, std::ios::end );
				length = fragmentFile.tellg();
				fragmentFile.seekg ( 0, std::ios::beg );
				fragmentSource=new char[length];
				for(int i=0;i<length;i++)
					fragmentSource[i]='\0';
				fragmentFile.read ( fragmentSource,length );
				//printf("\n***********\n%s\n***********\n",fragmentSource);
				fragmentShader=glCreateShaderObjectARB ( GL_FRAGMENT_SHADER_ARB );

				//shaderSource takes an array of strings, so create one even if we only have one string
				const char * vv = fragmentSource;
				glShaderSourceARB ( fragmentShader, 1, &vv, ( GLint* ) lengthv );
				delete [] fragmentSource;
				//inputForHash+=fragmentSource; //to create the md5 hash

				//delete fragmentSource; //release shader source memory;
#ifdef PRINTFXLOADINGMESSAGES

				printf ( "Compiling fragment Shader...\n" );
#endif

				glCompileShaderARB ( fragmentShader );
				
				int compileStatus=getShaderCompileStatus(fragmentShader);
				
				if ( !compileStatus )
				{
					std::string infoLog;
					infoLog = getInfoLog ( fragmentShader );
					printf ( "Vertex Shader compilation error!:\n%s\nFX not loaded!\n",infoLog.c_str() );
					compilationError="Vertex Shader Compilation Error: ";
					compilationError+=vertex;
					compilationError+=":\n\n";
					compilationError+=infoLog;
					//delete [] shaderPath;
					if ( pprogress!=NULL )
					{
						pprogress->label ( "Vertex Shader Error" );
						pprogress->value ( 0 );
						pprogress->selection_color (  fl_rgb_color(42,20,20) );
						app().processEvents();
					}
					errorWhileLoading=true;
					//return 1;
				}
//				if ( strstr ( infoLog.c_str(),"ERROR" ) !=NULL || strstr ( infoLog.c_str(),"error" ) !=NULL )
//				{
//					printf ( "Fragment Shader compilation error!:\n%s\nFX not loaded!\n",infoLog.c_str() );
//					
//					compilationError="Fragment Shader Compilation Error: ";
//					compilationError+=fragment;
//					compilationError+=":\n\n";
//					compilationError+=infoLog;
//					//delete shaderPath;
//					if ( pprogress!=NULL )
//					{
//						pprogress->label ( "Fragment Shader compilation error!" );
//						pprogress->value ( pprogress->maximum() );
//						pprogress->selection_color (  fl_rgb_color(42,20,20) );
//						app().processEvents();
//					}
//					errorWhileLoading=true;
//					//return 1;
//				}
//				else
//				{
//					if ( strstr ( infoLog.c_str(),"warning" ) !=NULL )
//					{
//						printf ( "...OK! (With Warnings)\n" );
//#ifdef PRINTFXLOADINGMESSAGES
//
//						printf ( "    Fragment Shader compilation warnings!:\n%s\nFX still loaded!\n",infoLog.c_str() );
//#endif
//
//					}
//					else
//						printf ( "..OK!\n" );


				//}
			}
		}
		
		//Compile shader program
#ifdef PRINTFXLOADINGMESSAGES
		printf ( "Compiling Shader Program\n" );
#endif

		ShaderProgram=glCreateProgramObjectARB();
		while ( ShaderProgram==0 )
		{
			ShaderProgram=glCreateProgramObjectARB();
			glPrintError();
		}

		glAttachObjectARB ( ShaderProgram,vertexShader );
		glAttachObjectARB ( ShaderProgram,fragmentShader );

		glLinkProgramARB ( ShaderProgram );
		std::string infoLog;
		infoLog = getInfoLog ( ShaderProgram );
		if ( ( strstr ( infoLog.c_str(),"error" ) !=0 ) )
		{
			printf ( "Shader Program Linking error!:\n%s\nFX not loaded!\n",infoLog.c_str() );
			if ( pprogress!=NULL )
			{
				pprogress->label ( "Shader Program Linking error!" );
				pprogress->value ( pprogress->maximum() );
				pprogress->selection_color (  fl_rgb_color(42,20,20) );
				app().processEvents();
			}
			errorWhileLoading=true;
			//return 1;
		}
		else
		{
			loadedAndCompiled=true;

			//calculate the FXs content hash.
			// JEF-28: hash the RAW BYTES of the .jfx + .vert + .frag files
			// (portable — same digest on any build/platform, and it now
			// COVERS THE SHADER SOURCE, which the old metadata-only hash
			// ignored — two FX with identical metadata but different GLSL
			// used to collide). shaderPath/vertex/fragment are the same paths
			// the shader load above used. Falls back to the metadata hash only
			// if none of the files can be re-read here.
			md5Hash=jefe::contentHashFiles ( { pfilename, shaderPath+vertex, shaderPath+fragment } );
			if ( md5Hash.empty() ) md5Hash=GetMD5Hash ( inputForHash );
			//printf ( "\nFX Hash:lenght=%s:%i\n",md5Hash.c_str(),md5Hash.size() );

			if ( pprogress!=NULL )
			{
				pprogress->selection_color (  fl_rgb_color(20,42,20) );
				//pprogress->labelcolor ( FL_WHITE );
				pprogress->label ( "Shader Program compiled correctly, FX ready to use!" );
				pprogress->value ( pprogress->maximum() );

				app().processEvents();
			}
			//printf ( " FX ready to use!\n" );
		}
	}
	//end of loading shaders


	//bind();

	//printf("************************\n");

	return 0;
}

void gfcFX::reset()
{
	//iterate all groups and all widgets, setting the value to de defaultValue;
	std::map<std::string,gfcFXWidgetGroup>::iterator groupsIter=groups.begin();
	std::map<std::string,gfcFXWidgetGroup>::iterator groupsIterEnd=groups.end();
	for ( groupsIter;groupsIter!=groupsIterEnd;groupsIter++ )
	{
		std::map<std::string,gfcFXWidget>::iterator widgetsIter=groupsIter->second.widgets.begin();
		std::map<std::string,gfcFXWidget>::iterator widgetsIterEnd=groupsIter->second.widgets.end();
		for ( widgetsIter;widgetsIter!=widgetsIterEnd;widgetsIter++ )
		{
			widgetsIter->second.value=widgetsIter->second.defaultValue;
		}

	}

}

PlateFXParams gfcFX::bind ( int previousTexID, FXTexCoords fboTexCoords, bool forcedLoading )
{



	PlateFXParams params;
	params.pass=FXPASS_INTERMEDIATE;

	std::string texCoordLocationNames[4]={"texCoord0","texCoord1","texCoord2","texCoord3"};
	//std::string texCoordYLocationNames[4]={"texCoord0.y","texCoord1.y","texCoord2.y","texCoord3.y"};
	
	if ( loadedAndCompiled )
	{
		//printf("Binding!\n");
		//bind the shader variables
		//bind the program
		//printf("Binding PO: %i\n",ShaderProgram);
		glGetError();
		glUseProgramObjectARB ( ShaderProgram );
		glPrintError();
		//printf("Shader Info:\n *Name: %s\n *Active shader %i, should be: %i\n",name, glGetHandleARB(GL_PROGRAM_OBJECT_ARB),ShaderProgram);

		int CubeUnitCounter=4; //each texture accesed by the shader must be in a different texture unit, so each time we bind a texture or cube, we increment this by 1 and use it as an offset to GL_TEXTURE0.
		int textureCounter=0; //keep track of weather we have a texture in the controls or not, if not, we use the fboInfo passed as a parameter
		

		//NEW STUFF!! Pass some additional program state data to the shader, it can use it if it wants, the data is also sent to the shader before the params, so the controls
		//can override the values if they want, but I can't see why that would be needed. 

		//pass current frame number
		GLuint location=glGetUniformLocationARB ( ShaderProgram,"currentFrame");
		if(location!=-1){
		 glUniform1fARB ( location,playbackManager.getCurrentFrame());
		 //printf("location for currentFrame=%i\n",location);
		}
		//pass target FPS (do we really need this?)
		location=glGetUniformLocationARB ( ShaderProgram,"targetFPS");
		if(location!=-1)
			glUniform1fARB ( location,playbackManager.getTargetFPS());

		//pass current timestep
		location=glGetUniformLocationARB ( ShaderProgram,"timestep");
		if(location!=-1)
			glUniform1fARB ( location,playbackManager.getTimestep());
		

		//the widgets and variables should be iterated using the ordered vector, since we want to bind the textures in the correct texture unit, not in alphabetical order (wich iterating throug the map would give us). It is the same problem as iterating throug the map when creating the FX's GUI
		std::map<std::string,gfcFXWidgetGroup>::iterator groupsIter=groups.begin();
		std::map<std::string,gfcFXWidgetGroup>::iterator groupsIterEnd=groups.end();
		
		for ( groupsIter;groupsIter!=groupsIterEnd;groupsIter++ )
		{


			/*std::map<std::string,gfcFXWidget>::iterator widgetsIter=groupsIter->second.widgets.begin();
			std::map<std::string,gfcFXWidget>::iterator widgetsIterEnd=groupsIter->second.widgets.end();*/

			std::vector<std::string>::iterator widgetIter=groupsIter->second.widgetsOrder.begin();
			std::vector<std::string>::iterator widgetIterEnd=groupsIter->second.widgetsOrder.end();

			for ( widgetIter;widgetIter!=widgetIterEnd;widgetIter++ )
			{
				//printf("Binding %s\n",widgetsIter->second.varName);
				//groupsIter->second.widgets[*widgetIter]; //get the actual widget from the map using the name from the
				switch ( groupsIter->second.widgets[*widgetIter].type )
				{

					case FX_GUI_BOOL:
					case FX_GUI_CHOICE:
					case FX_GUI_FLOAT:
					{

						GLuint location=glGetUniformLocationARB ( ShaderProgram,groupsIter->second.widgets[*widgetIter].varName.c_str() );
						//printf("-Binding location %i with value %f to program %i\n",location, groupsIter->second.widgets[*widgetIter].value, ShaderProgram);

						glUniform1fARB ( location,groupsIter->second.widgets[*widgetIter].value );

						//printf("-finished location %i binding\n",location);
					}
					break;

					case FX_GUI_CUBE:
					{

						glActiveTexture ( GL_TEXTURE0+CubeUnitCounter );
						//do a switch to figure out from what sequence we will assign the texture, or if we use the previousTexID

						//printf("binding 3D cube: %i to variable %s in unit GL_TEXTURE0+%i\n",lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture3D,groupsIter->second.widgets[*widgetIter].varName.c_str(),CubeUnitCounter);
						glBindTexture ( GL_TEXTURE_3D,lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture3D );

						glUniform1iARB ( glGetUniformLocationARB ( ShaderProgram,groupsIter->second.widgets[*widgetIter].varName.c_str() ),   CubeUnitCounter ); //the parameter we assign to the uniform variable is the texture unit this texture was assigned to.

						CubeUnitCounter++;
				
						
						glActiveTexture ( GL_TEXTURE0 );
						
						
						//also pass the lutSize to use in 3d lut calculations
						{
							GLuint location=glGetUniformLocationARB(ShaderProgram,std::string(groupsIter->second.widgets[*widgetIter].varName+"_size").c_str());
							if(location!=-1){
								glUniform1fARB(location, (float)lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).size);
					
							}
						}


						//glUniform1f(glGetUniformLocationARB(ShaderProgram,groupsIter->second.widgets[*widgetIter].varName.c_str()),groupsIter->second.widgets[*widgetIter].value);
					}
					break;

					case FX_GUI_LUT:
					{
						glEnable ( GL_TEXTURE_1D );
						glActiveTexture ( GL_TEXTURE0+CubeUnitCounter );
						//do a switch to figure out from what sequence we will assign the texture, or if we use the previousTexID

						//printf("binding 1D cube: %i to variable %s in unit GL_TEXTURE0+%i\n",lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture1D,groupsIter->second.widgets[*widgetIter].varName.c_str(),CubeUnitCounter);
						GLuint location=glGetUniformLocationARB ( ShaderProgram,groupsIter->second.widgets[*widgetIter].varName.c_str() );
						glBindTexture ( GL_TEXTURE_1D,lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture1D );
						//printf("Bindging texture %i to GL_TEXTURE_1D on location %i\n",lutManager.getLUT(groupsIter->second.widgets[*widgetIter].value).texture1D,location);
						
						glUniform1iARB ( location,   CubeUnitCounter ); //the parameter we assign to the uniform variable is the texture unit this texture was assigned to.

						CubeUnitCounter++;


						glActiveTexture ( GL_TEXTURE0 );

						//glUniform1f(glGetUniformLocationARB(ShaderProgram,groupsIter->second.widgets[*widgetIter].varName.c_str()),groupsIter->second.widgets[*widgetIter].value);
					}
					break;

					case FX_GUI_TEXTURE:


						glActiveTexture ( GL_TEXTURE0+textureCounter );
						
							//pass current textureCoordinates size
							
						

						//do a switch to figure out from what sequence we will assign the texture, or if we use the previousTexID						
						int textureCase=( int ) groupsIter->second.widgets[*widgetIter].value;
						
						switch ( textureCase )
						{
							case 0: //use previousTexID

								glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,previousTexID );

								glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
								glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
								params.texCoords[textureCounter]=fboTexCoords;

								//pass the size of the texture as a whole also, som shader might use it
								location=glGetUniformLocationARB ( ShaderProgram,texCoordLocationNames[textureCounter].c_str());
								if(location!=-1)
								{
									
									glUniform2fARB ( location,fboTexCoords.s,fboTexCoords.y );
									//printf("**%s(%i): %f %f\n",texCoordLocationNames[textureCounter].c_str(),textureCounter,(float)fboTexCoords.s,(float)fboTexCoords.y);
								}
								/*location=glGetUniformLocationARB ( ShaderProgram,texCoordXLocationNames[0].c_str());
								if(location!=-1) glUniform1fARB ( location,fboTexCoords.r );

								location=glGetUniformLocationARB ( ShaderProgram,texCoordYLocationNames[0].c_str());
								if(location!=-1) glUniform1fARB ( location,fboTexCoords.s );*/


								break;
							case 1:
							case 2:
							case 3: //Get textures from the trackManager
							case 4:
							{
								gfcFrame tmpFrame = trackManager.getSequence(groupsIter->second.widgets[*widgetIter].value-1)->getFrame(playbackManager.getCurrentFrame(),forcedLoading);
								glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,tmpFrame.textureID );
								//TODO: Contemplate the case where the texture is compressed so we don't use texture rectangle
								//set the tex coords in the params, remember that we have to invert the vertical texture coordinates (t for y).
								params.texCoords[textureCounter].x=0;
								params.texCoords[textureCounter].y=0;
								params.texCoords[textureCounter].s=tmpFrame.sizeX;
								params.texCoords[textureCounter].t=tmpFrame.sizeY;
								
								//pass the size of the texture as a whole also, some shader might use it
								location=glGetUniformLocationARB ( ShaderProgram,texCoordLocationNames[textureCounter].c_str());
								//printf("Finding location for texCoordLocationNames[%i]=%s =%i shader: %i\n",textureCounter,texCoordLocationNames[textureCounter].c_str(),location,ShaderProgram);
								if(location!=-1)
								{
									
									glUniform2fARB ( location,tmpFrame.sizeX,tmpFrame.sizeY);
									//printf("%s(%i): %f %f\n",texCoordLocationNames[textureCounter].c_str(),textureCounter,(float)tmpFrame.sizeX,(float)tmpFrame.sizeY);
								}
								/*location=glGetUniformLocationARB ( ShaderProgram,texCoordYLocationNames[textureCase-1].c_str());
								if(location!=-1){
									glUniform1fARB ( location,tmpFrame.sizeY );
									printf("Sending Tex CoordY: %f\n",tmpFrame.sizeY );
								}*/

								/*params.texCoords[textureCounter].s=tmpFrame.texCoords.w;
								params.texCoords[textureCounter].t=tmpFrame.texCoords.h;*/

							}
							break;


						}
						glUniform1iARB ( glGetUniformLocationARB ( ShaderProgram, ( groupsIter->second.widgets[*widgetIter].varName.c_str() ) ),textureCounter ); //the parameter we assign to the uniform variable is the texture unit this texture was assigned to.
						glActiveTexture ( GL_TEXTURE0 );
						textureCounter++;

						break;
				}

			}

			//we have to consider the case where the FX specifies no texture, so we use the fboTexture, this case is true when texturCounter is 0;
			if ( textureCounter==0 )
			{
				glActiveTexture ( GL_TEXTURE0);

				glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,previousTexID );
				glBindTexture ( GL_TEXTURE_RECTANGLE_ARB,previousTexID );
				glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
				glTexParameteri ( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
				params.texCoords[textureCounter]=fboTexCoords;
				params.texCoords[textureCounter]=fboTexCoords;
				location=glGetUniformLocationARB ( ShaderProgram,texCoordLocationNames[textureCounter].c_str());
				//printf("Finding location for texCoordLocationNames[%i]=%s =%i shader: %i\n",textureCounter,texCoordLocationNames[textureCounter].c_str(),location,ShaderProgram);
				if(location!=-1)
				{

					glUniform2fARB ( location,fboTexCoords.s,fboTexCoords.y);
					//printf("%f %f %f %f\n",fboTexCoords.x, fboTexCoords.y, fboTexCoords.r, fboTexCoords.s);
				}
				textureCounter=1;

			}

			params.numberOfTextures=textureCounter;
		}




	}
	//printf("Done Binding!\n");
	return params;
}

void gfcFX::unbind()
{
	if ( loadedAndCompiled )
		glUseProgramObjectARB ( 0 );
		
	
	
}

void gfcFX::freeResources()
{
	glDetachObjectARB(ShaderProgram,vertexShader);
	glDeleteObjectARB(vertexShader);
	glDetachObjectARB(ShaderProgram,fragmentShader);
	glDeleteObjectARB(fragmentShader);
	glDeleteObjectARB(ShaderProgram);
}
