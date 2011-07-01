#include "gfcpointerstorage.h"

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

gfcPointerStorage::gfcPointerStorage()
{
	maxPointerStore=80;
	fadeDelay=1.0/10.0;
	
	
}


gfcPointerStorage::~gfcPointerStorage()
{ 
}

void gfcPointerStorage::store(gfcNetRemotePointerInfo info)
{
	info.fadeCounter=1.0;
	pointerMap[info.name].push_front(info);
	if( pointerMap[info.name].size()>maxPointerStore && maxPointerStore>0) //if we are over the allowed size, pop the back.
	{
		pointerMap[info.name].pop_back();	
	}
	updateFaders();
	//std::cout << "PointerMap for " << info.name << " " << pointerMap[info.name].size() <<std::endl;
}

void gfcPointerStorage::updateFaders()
{
	bool drawFlag=false; //this is one of the conditions that need to be true to trigger a redraw, next to a certain amount of time to have happened.
	this->timeSinceLastRedraw+=playbackManager.getTimestep();
	std::map<std::string, std::deque<gfcNetRemotePointerInfo> >::iterator nickIter=pointerMap.begin(), end=pointerMap.end();
	//loop for each nickname, each one which has a pointer and a queue of trails.
	for( nickIter;nickIter!=end; /*nickIter++*/ ){ //the iterators advancement are now done below.
		float fadeDelta=playbackManager.getTimestep()*fadeDelay;
		
		nickIter->second.front().fadeCounter-=fadeDelta;
		
		int numToPop= maxPointerStore*(1.0-nickIter->second.front().fadeCounter);
		if(maxPointerStore==0)
		{
			numToPop=nickIter->second.size()-1;
		}
		else
		{
			//if we pop a trail then that would trigger a redraw
			drawFlag=true;
		}
		
		//printf("Num to pop %i\n",numToPop);
		for(int i=0;i<numToPop && nickIter->second.size()>1;i++)
		{
			nickIter->second.pop_back();
			
			
			if(nickIter->second.size()==1)
			{
			nickIter->second.front().fadeCounter=1.0;
			break;
			}
			
		}
		
		if(nickIter->second.size()==1)
		{
			
			nickIter->second.front().fadeCounter-=fadeDelta*10.0;
			if(nickIter->second.front().fadeCounter<0){
				nickIter->second.pop_front();
				//if we popped the last one, then we force the redraw, since who knows if the time has passed and the flag will go on.
				plateManager.setChanged();
			}

			
		}
		
		
		
		if(nickIter->second.empty())
		{
			pointerMap.erase(nickIter++); //we post increment to make sure we have a valid next pointer after erasing.
		}
		else
		{
			nickIter++; //if we dont erase, increment the iter anyway.
		}
		
	}
	
	if(timeSinceLastRedraw>0.033 && drawFlag){ //redraw pointers at 60fps
		plateManager.setChanged();
		timeSinceLastRedraw=0;
	}
    	
	
}






void gfcPointerStorage::removeFromMap(gfcNetRemotePointerInfo info)
{
	
	
	if(pointerMap.find(info.name)!=pointerMap.end())
	{
		pointerMap.erase(pointerMap.find(info.name));
		printf("clearing from map\n");
	}
	
	
}

bool gfcPointerStorage::empty()
{
	std::map<std::string, std::deque<gfcNetRemotePointerInfo> >::iterator iter=pointerMap.begin(), end=pointerMap.end();
	//printf("mapSize=%i\n",pointerMap.size());
	for( iter;iter!=end ;iter++ ){
		if(!iter->second.empty())
			return false; //if any deque is not empty, we return false
	}
	return true; //if we found all empty deques then return true
	
}