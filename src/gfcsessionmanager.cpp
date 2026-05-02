#include "gfcsessionmanager.h"

#include <stdio.h> //for remove

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;


#include "gfcStructures.h"
extern gfcSettings sett;



void rebuildRecentSessionsMenu();



gfcSessionManager sessionManager;

gfcSessionManager::gfcSessionManager()
{
	crashSessionName=getApplicationDataPath()+"recoverySession.jcs";
}


gfcSessionManager::~gfcSessionManager()
{
	removeCrashSession();
}

void gfcSessionManager::loadSession(std::string filename)
{
    if ( filename.empty() ) {
        printf ( "LoadSession: Error, filename is empty\n" );
        return;
    }
    
    //Prepare the program to receive the session.
    trackManager.stopLoadingAll();
    
    //1. Parse and check the XML File
    XMLNode xMainNode=XMLNode::openFileHelper ( filename.c_str() );
    if(!xMainNode.loaded)
    {
        printf("LoadSession: Error opening session file\n");
        return;
    }
    XMLNode xRootNode=xMainNode.getChildNode ( "root" );
    
    
    /****************/
    /*LOAD SETTINGS*/
    /***************/
    XMLNode settingsNode=xRootNode.getChildNode ( "settings" );
    
    plateManager.setFramingMode(readAttributeFromNode<int>("framingMode",settingsNode,0)+FRAMINGSINGLE_ID);
    sett.filterMin=sett.filterMax=readAttributeFromNode<int>("filtering",settingsNode,0)+GL_NEAREST;
    playbackManager.setPlaybackMode(readAttributeFromNode<int>("loopMode",settingsNode,0)+LOOPMODEONCE_ID);
    playbackManager.setLoopPriority(readAttributeFromNode<int>("loopPriority",settingsNode,0));
    playbackManager.setTargetFPS(readAttributeFromNode<float>("targetFPS",settingsNode,0));
    playbackManager.setFromFrame(readAttributeFromNode<int>("from",settingsNode,1));
    playbackManager.setToFrame(readAttributeFromNode<int>("to",settingsNode,1));


    /****************/
    /*LOAD PLATES   */
    /***************/
    XMLNode platesNode=xRootNode.getChildNode("plates");
    plateManager.loadPlateSessionParameters(platesNode);
    plateManager.updateAllFromGUI();
    /****************/
    /*LOAD TRACKS   */
    /***************/
    XMLNode tracksNode=xRootNode.getChildNode("tracks");
    trackManager.loadTrackSessionParameters(tracksNode);
    
	/****************/
	/*LOAD PLAYLIST   */
	/***************/
	XMLNode playlistNode=xRootNode.getChildNode("playlist");
	playlistManager.clearPlaylist();
	playlistManager.loadPlaylistParameters(playlistNode);

    //NOTE: save into recent sessions.
   // if all was good, save to the recent sessions
	
    if ( sett.recentSessions.size() <sett.maxRecentSessions ) {
        //check if the stack is not in the vector already.
        bool alreadyInRecent=false;
        for ( int i=0;i<sett.recentSessions.size();i++ ) {
            if ( sett.recentSessions[i]==filename ) {
                alreadyInRecent=true;
                break;
            }
        }

        if ( !alreadyInRecent ) {
            sett.recentSessions.push_back ( filename);
        }

    } else {
        {
            bool alreadyInRecent=false;
            for ( int i=0;i<sett.recentSessions.size();i++ ) {
                if ( sett.recentSessions[i]==filename ) { //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
                    alreadyInRecent=true;
                    sett.recentSessions.erase ( sett.recentSessions.begin() +i );
                    sett.recentSessions.push_back ( filename );
                    break;
                }
            }

            if ( !alreadyInRecent ) { //if it's not in the recent, then erase the first one and push the new one.
                sett.recentSessions.erase ( sett.recentSessions.begin() );
                sett.recentSessions.push_back ( filename );
            }


        }
    }
    
    rebuildRecentSessionsMenu();
}

void gfcSessionManager::saveSession(std::string filename)
{
if ( filename.empty() ) {
        printf ( "SaveSession Error: Empty filename\n" );
        return;
    }

    filename=AppendExtensionToFilename(filename,".jcs");

    //printf ( "Saving Session to %s\n",filename.c_str() );

    XMLNode xMainNode=XMLNode::createXMLTopNode ( "xml",TRUE );
    xMainNode.addAttribute ( "version","1.0" );
    XMLNode xRootNode=xMainNode.addChild ( "root" );
    xRootNode.addAttribute ( "comment", "This is a JefeCheck XML formated Session File" );

    char tmpValue[2048];

    /**************************/
    /*	SAVE SETTINGS INFO    */
    /**************************/

    XMLNode settingsNode=xRootNode.addChild ( "settings" );
    
    saveSetting("framingMode",plateManager.getFramingMode()-FRAMINGSINGLE_ID,settingsNode);
    saveSetting("bgColor",48,settingsNode);
    saveSetting("filtering",sett.filterMin-GL_NEAREST,settingsNode); //filter min and max are always the same now
    saveSetting("loopMode",playbackManager.getPlaybackMode()-LOOPMODEONCE_ID,settingsNode);
    saveSetting("loopPriority",playbackManager.getLoopPriority(),settingsNode);
    saveSetting("targetFPS",playbackManager.getTargetFPS(),settingsNode);
    saveSetting("from",playbackManager.getFromFrame(),settingsNode);
    saveSetting("to",playbackManager.getToFrame(),settingsNode);
    
    // Save plate parameters along with FX stacks
    XMLNode platesNode=xRootNode.addChild("plates");
    plateManager.savePlateSessionParameters(platesNode);
    
    //Save sequences parameters
    XMLNode tracksNode=xRootNode.addChild("tracks");
    trackManager.saveTrackSessionParameters(tracksNode);
	
	//save playlist
	XMLNode playlistNode=xRootNode.addChild("playlist");
	playlistManager.savePlaylistParameters(playlistNode);

    XMLError writeError=xMainNode.writeToFile ( filename.c_str() );

    if ( writeError!=eXMLErrorNone ) {
        printf ( "Error writing JCS file %s!\n",filename.c_str() );
    } else {
    	
    	//NOTE: save into recent sessions.
   // if all was good, save to the recent sessions
	
    if ( sett.recentSessions.size() <sett.maxRecentSessions ) {
        //check if the stack is not in the vector already.
        bool alreadyInRecent=false;
        for ( int i=0;i<sett.recentSessions.size();i++ ) {
            if ( sett.recentSessions[i]==filename ) {
                alreadyInRecent=true;
                break;
            }
        }

        if ( !alreadyInRecent ) {
            sett.recentSessions.push_back ( filename);
        }

    } else {
        {
            bool alreadyInRecent=false;
            for ( int i=0;i<sett.recentSessions.size();i++ ) {
                if ( sett.recentSessions[i]==filename ) { //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
                    alreadyInRecent=true;
                    sett.recentSessions.erase ( sett.recentSessions.begin() +i );
                    sett.recentSessions.push_back ( filename );
                    break;
                }
            }

            if ( !alreadyInRecent ) { //if it's not in the recent, then erase the first one and push the new one.
                sett.recentSessions.erase ( sett.recentSessions.begin() );
                sett.recentSessions.push_back ( filename );
            }


        }
    }
    
    rebuildRecentSessionsMenu();
        //printf ( "Session saved to %s\n",filename.c_str() );
    }

}

void gfcSessionManager::writeCrashSession()
{
	if(sett.enableCrashRecoverySession)
		saveSession(crashSessionName);
}

bool gfcSessionManager::checkCrashedSession()
{
	if(sett.enableCrashRecoverySession)
		return fileExists(crashSessionName);
	return false;
}

void gfcSessionManager::loadCrashedSession()
{
	if(fileExists(crashSessionName)){
		loadSession(crashSessionName);
		removeCrashSession();	
	}
}

void gfcSessionManager::removeCrashSession()
{
	remove(crashSessionName.c_str());
}


void rebuildRecentSessionsMenu() {
}
