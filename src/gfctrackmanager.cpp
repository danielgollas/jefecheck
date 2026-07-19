

#include "gfctrackmanager.h"
#include "qt/gfcsequencegui_qt.h"

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

gfcTrackManager::gfcTrackManager()
{
	numOfSequences=GFC_MAX_SEQUENCES;
	autoAcceptRemoteLoadRequests=false;
}




gfcTrackManager::~gfcTrackManager()
{
	//TODO: Implement me!
}

void gfcTrackManager::startLoadingSequence(int whichOne, gfcLoadParams params)
{
	//TODO: Implement me!
}

void gfcTrackManager::startLoadingSequence(int whichOne)
{
	plateManager.clearAllHistogramCache();
	if(whichOne>=numOfSequences || whichOne<0)
	{
		printf("\nError: Track manager was requested an out of range sequence\n");
		return;
	}
	
	
	sequences[whichOne].startLoading();
	playbackManager.setFromFrame(1);
	playbackManager.setToFrame(this->getMaxTrackLength());
	// The Qt timeline view spans [in, out]; default it to the full content
	// so a freshly-loaded sequence fills the timeline width.
	playbackManager.setInPoint(1);
	playbackManager.setOutPoint(this->getMaxTrackLength());

	updateTrackWidgetsFromAndTo(playbackManager.getFromFrame(),playbackManager.getToFrame());

}


gfcPlaylistItem gfcTrackManager::getPlaylistItem()
{
	gfcPlaylistItem result;
	
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		result.loadParams.push_back(sequences[i].getLoadParamsFromGUI());
	}
	
	result.fxstacks=plateManager.getPlateFXStacks();
	result.programState=getCurrentProgramState();
	return result;
}

void gfcTrackManager::setAutoAcceptRemoteLoadRequests(int value)
{
	autoAcceptRemoteLoadRequests=value;
}
int gfcTrackManager::getAutoAcceptRemoteLoadRequests()
{
	return autoAcceptRemoteLoadRequests;
}

void gfcTrackManager::setPlaylistItem(gfcPlaylistItem item, bool fromNetwork){

	gfcLoadParams dummy;
	
	if (fromNetwork && !autoAcceptRemoteLoadRequests)
	{
		//ask if accept, accept and change setting, or decline
		std::string message="A user in the remote session is\nloading a playlist item which contains the sequences\n";
		for (int i=0;i<item.loadParams.size();i++)
		{
			std::string theFilename=GetFilenameNoPath(item.loadParams[i].fileName);
			if(theFilename!=""){
			message+=theFilename;
			message+="\n";
			}
		}
		message+="\nWould you like to load this item too?";

		// No interactive prompt in the Qt build yet — accept silently.
		int answer = 0;
		(void)message;

		switch ( answer ) {
		case 0:
			//load the item
			sessionManager.removeCrashSession();
			break;
		case 1:
			//load the item and set autoload
			sessionManager.loadCrashedSession();
			setAutoAcceptRemoteLoadRequests(true);
			break;

		case 2:
			//don't load zit.
			return;
			break;
		}
	}
	
	gfcPlaylistItem itemToSend = item; //we make a copy because we might modify the original with the scaleOverride.

	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(i<item.loadParams.size()){
			//apply the scale override first
			if (scaleOverride>0)
			{
				item.loadParams[i].scale=scaleOverride;
			}
			
			sequences[i].setLoadParamsToGUI(item.loadParams[i]);
		}
		else
		{
			sequences[i].setLoadParamsToGUI(dummy);
		}
	}

	plateManager.setPlateFXStacks(item.fxstacks);
	
	setCurrentProgramState(item.programState);

	startLoadingAll();
	networkManager.sendPlaylistItem(itemToSend);
}

void gfcTrackManager::startLoadingAll(std::vector< gfcLoadParams > params)
{
//TODO: Implement me!
}

void gfcTrackManager::startLoadingAll()
{

	plateManager.clearAllHistogramCache();
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].startLoading();
	}
	//printf("Set all to load!\n");
	
	
	playbackManager.setFromFrame(1);
	playbackManager.setToFrame(this->getMaxTrackLength());

	playbackManager.setInPoint(1);
	playbackManager.setOutPoint(this->getMaxTrackLength());
	
	updateTrackWidgetsFromAndTo(playbackManager.getFromFrame(),playbackManager.getToFrame());
	
}

void gfcTrackManager::clearAllSequences()
{
	plateManager.clearAllHistogramCache();
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].clearSequence();
	}
}

gfcSequence * gfcTrackManager::getSequence(int whichOne)
{

	if(whichOne>=numOfSequences || whichOne<0)
	{
		printf("\nError: Track manager was requested an out of range sequence\n");
		return &dummySequence;
		
	}
	else{
		return &sequences[whichOne];
	}

}

int testHandle()
{
	printf("Handle!\n");
	return 0;
}

void gfcTrackManager::initializeWidgets()
{
    // Qt: each sequence gets a stateful gfcSequenceGUI_Qt. The drop
    // handler / future load panel will push filename + load params via
    // setters; loadPreview reads them back through getXxx().
    for (int i = 0; i < GFC_MAX_SEQUENCES; ++i) {
        sequences[i].myGUI  = new gfcSequenceGUI_Qt;
        sequences[i].trackID = 'A' + i;
    }
}

void gfcTrackManager::stopLoadingAll()
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].stopLoading();
	}
}

void gfcTrackManager::stopLoadingSequence(int whichOne)
{
	if(whichOne>=numOfSequences || whichOne<0)
	{
		printf("\nError: Track manager was requested an out of range sequence\n");
		return;
	}
	
	sequences[whichOne].stopLoading();
}

int gfcTrackManager::getMaxTrackLength()
{
	//TODO: Implement Me
	int max=0;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(sequences[i].getNumFrames()>max)
			max=sequences[i].getNumFrames();
	}
	return max;
}

int gfcTrackManager::getMinTrackLength()
{
	//TODO: Implement Me
	return 0;
}




/**
 * Tells one of it's tracks to load a previewFrame.
 * @param whichOne What track do we want to load the preview frame for.
 */
void gfcTrackManager::loadPreviewFrame(int whichOne)
{

	if(checkBounds(whichOne,"loadPreview"))
	{
		std::string loadedFileName=sequences[whichOne].loadPreview();
		if(loadedFileName!="")
		addToRecentlyLoaded(loadedFileName);
	}
	
	plateManager.clearAllHistogramCache();
}

/**
 * Checks if whichOne is whithin the bounds of the track array.
 * @param whichOne number to check
 * @param functionName If an error ocurs, this gets appended to the error message.
 * @return Validity of whichOne within the bounds.
 */
bool gfcTrackManager::checkBounds(int whichOne, std::string functionName)
{
	if(whichOne>=numOfSequences || whichOne<0)
	{
		printf("\ngfcTrackManager::%s: ERROR Requested an out of bounds Sequence\n",functionName.c_str());
		return false;
	}
	else
	{
		return true;
	}
}

/**
 * Iterates the sequences and calls their createTexture method. Creating the texture is the last step in loading a frame, but must be done in the main thread, and therefore cannot be encapsulated in the same function as the rest of the loading process. It works in a producer-consumer threding model. Each sequences thread loads and produces "raw frames", and the main thread tells those objects to create openGL textures and "finished frames" from those raw ones.
 */
void gfcTrackManager::generateTextures()
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].generateTextures(1);
	}
}

void gfcTrackManager::updateTrackWidgetsFromAndTo(int visibleFrom, int visibleTo)
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].setVisibleFromAndTo(visibleFrom,visibleTo);
	}
}

void gfcTrackManager::updateTrackWidgetsCurrentFrame(int currentFrame)
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		//sequences[i].setVisibleFromAndTo(visibleFrom,visibleTo);
	}

}

int gfcTrackManager::getTrackIDfromWidget(void* widget)
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(sequences[i].myGUI->widgetBelongsToMe(widget))
		  return i;
	}
	return -1;
}

/**
 * Identifies the track that has the earliest first loaded frame. This includes the offset, so a track that started loading the 10th frame but is offset by -5 will have the earliest loaded frame than a track that started loading on frame 6 ie.
 * @return The timeline position of the earliest loaded frame in the tracks.
 */
int gfcTrackManager::getFirstFirstLoaded()
{
	int winner=999999;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(!sequences[i].isEmpty() && sequences[i].getRangeStart()<winner)
		  winner=sequences[i].getRangeStart();
	}
	if(winner==999999)
		winner=0;
	return winner;
	
}

/**
 * Identifies the track that has the latest first loaded frame. That means, the track whos start is the farthest along the timeline, including the offset.
 * @return The timeline position of the latest first loaded frame in the tracks.
 */
int gfcTrackManager::getLastFirstLoaded()
{
	int winner=-999999;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(!sequences[i].isEmpty() &&  sequences[i].getRangeStart()>winner)
		  winner=sequences[i].getRangeStart();
	}
	if(winner==-999999)
		winner=0;
	return winner;
}

/**
 * Identifies the track that has the earliest last loaded frame. That means, the track who's end (loaded end, not total frames end) is the earliest on the timeline.
 * @return The timeline position of the earliest last loaded frame in the tracks.
 */
int gfcTrackManager::getFirstLastLoaded()
{
	int winner=99999999;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(!sequences[i].isEmpty() && sequences[i].getRangeEnd()<winner)
		  winner=sequences[i].getRangeEnd();
	}
	if(winner==99999999)
		winner=0;
	return winner;
}

/**
 * Identifies the track that has the last last loaded frame. That means, the track who's end (loaded end, not total frames end) is the latest on the timeline.
 * @return The timeline position of the last last loaded frame in the tracks.
 */
int gfcTrackManager::getLastLastLoaded()
{
	int winner=-9999999;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		if(!sequences[i].isEmpty() && sequences[i].getRangeEnd()>winner)
		  winner=sequences[i].getRangeEnd();
	}
	if(winner==-9999999)
		winner=0;
	return winner;
}

/**
 * Adds the parameter filename to the recently loaded frames vector, reordering the vector if it was already there. Updates each tracks recentlyLoaded GUI 
 according to the list. 
 * @param filename 
 */
void gfcTrackManager::addToRecentlyLoaded(std::string filename)
{

	if ( recentBrowsed.size() <sett.maxRecentBrowsed )
	{
		//check if the stack is not in the vector already.
		bool alreadyInRecent=false;
		int i;
		for ( i=0;i<recentBrowsed.size();i++ )
		{
			if ( recentBrowsed[i]==filename )
			{
				alreadyInRecent=true;
				break;
			}
		}
			if(alreadyInRecent)
				recentBrowsed.erase(recentBrowsed.begin()+i);
			recentBrowsed.push_back ( filename );
		
	}
	else
	{
		{
			bool alreadyInRecent=false;
			for ( int i=0;i<recentBrowsed.size();i++ )
			{
				if ( recentBrowsed[i]==filename )
				{ //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
					alreadyInRecent=true;
					recentBrowsed.erase ( recentBrowsed.begin() +i );
					recentBrowsed.push_back ( filename );
					break;
				}
			}

			if ( !alreadyInRecent ) //if it's not in the recent, then erase the first one and push the new one.
			{
				recentBrowsed.erase ( recentBrowsed.begin() );
				recentBrowsed.push_back ( filename );
			}


		}
	}

	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].setRecentlyLoaded(recentBrowsed);
	}
	sett.recentBrowsed=recentBrowsed;
}

void gfcTrackManager::setScaleOverride(int scale)
{
	scaleOverride=scale;
}

void gfcTrackManager::setRecentBrowsed(std::vector< std::string > precentBrowsed)
{
	recentBrowsed=precentBrowsed;
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].setRecentlyLoaded(recentBrowsed);
	}
}

void gfcTrackManager::startLoadingSequenceAt(int whichOne, int startFrame)
{
	
	sessionManager.writeCrashSession();
	if(checkBounds(whichOne,"startLoadingSequenceAt"))
	{
		sequences[whichOne].startLoading(startFrame);
	}
	else
	{
		return;
	}
	plateManager.clearAllHistogramCache();
	sessionManager.writeCrashSession();
	
	
}

void gfcTrackManager::startLoadingAllAt(int startFrame)
{
	//when we load from the timeline (which is when we call this function), we tell the sequences that the track relative frame to start loading is 0, and give them the timeline relative frame to start loading from. This way, each track knows that it must account for their offset relative to the timeline.
	plateManager.clearAllHistogramCache();
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].startLoading(0,startFrame); 
	}
	
}





void gfcTrackManager::setForceGFLLoading(bool value)
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].setForceGFLLoading(value); 
	}

}

void gfcTrackManager::setContinueLoadingOnError( bool value )
{
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		sequences[i].setContinueLoadingOnError(value); 
	}
}

std::vector< gfcNetTrackStateInfo > gfcTrackManager::getTrackStateInfo()
{
	std::vector< gfcNetTrackStateInfo > result;
	
	for(int i=0;i<GFC_MAX_SEQUENCES;i++){
		result.push_back(sequences[i].getTrackStateInfo()); 
	}	
	
	return result;
}

void gfcTrackManager::setTrackStateInfo(std::vector< gfcNetTrackStateInfo > info)
{
	int size=GFC_MAX_SEQUENCES;
	int isize=info.size();
	for (int i=0;i<size && i<isize;i++) { //set transformations for each plate
        	this->sequences[i].setTrackStateInfo(info[i]);
        }

}

void gfcTrackManager::loadFromFilename(int whichOne, gfcLoadParams params)
{
	if(checkBounds(whichOne,"loadFromFilename"))
	{
		gfcSequence* tmp=&sequences[whichOne];
		tmp->myGUI->setFilename(params.fileName);
		char tmpScale[10];
		sprintf(tmpScale,"%i",(int)params.scale);
		tmp->myGUI->setScale(tmpScale);
		loadPreviewFrame(whichOne);
		
		//after loading the preview we can set the from-to frames
		if(params.fromFrame!=-1)
		tmp->myGUI->setFromFrame(params.fromFrame);
		
		if(params.toFrame!=-1)
		tmp->myGUI->setToFrame(params.toFrame);
		
		startLoadingSequence(whichOne);
	}
	else
	{
		return;
	}

}

void gfcTrackManager::saveTrackSessionParameters(XMLNode & tracksNode)
{
	int size=GFC_MAX_SEQUENCES;
	for (int i=0;i<size;i++) { //save info for each track
		XMLNode trackNode=tracksNode.addChild("track");
		saveSetting("trackID",i,trackNode);
        	sequences[i].saveTrackSessionParameters(trackNode);
        }
}

void gfcTrackManager::loadTrackSessionParameters(XMLNode & tracksNode)
{
	int tsize=tracksNode.nChildNode("track");
	int iterator=0;
	for (int i=0;i<tsize;i++) { //load info for each track
		
		XMLNode trackNode=tracksNode.getChildNode("track",&iterator);
		int trackID=readAttributeFromNode<int>("trackID",trackNode,GFC_MAX_SEQUENCES);
		
		if(trackID<GFC_MAX_SEQUENCES)
			sequences[trackID].loadTrackSessionParameters(trackNode);
        }
}

void gfcTrackManager::updateTrackWidgets()
{
	int size=GFC_MAX_SEQUENCES;
	for (int i=0;i<size;i++) { //save info for each track
        	sequences[i].updateTrackWidget();
        }
}

void gfcTrackManager::cleanForcedLoaded()
{
	int size=GFC_MAX_SEQUENCES;
	for (int i=0;i<size;i++) { //save info for each track
        	sequences[i].cleanForcedLoad();
        }
}