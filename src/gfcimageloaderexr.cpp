#include "gfcimageloaderexr.h"

#include <string>
#include <set>

#include "UIConstants.h"
 
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfTiledRgbaFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfTiledInputFile.h>
#include <OpenEXR/ImfPreviewImage.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/Iex.h>
#include <Imath/ImathMath.h>
#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>
#include <Imath/ImathFun.h>
#include <Imath/ImathMath.h> // halfFunction removed in modern Imath
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfKeyCode.h>
#include <OpenEXR/ImfTimeCode.h>
//#include <OpenEXR/half.h>
//#include <OpenEXR/halfLimits.h>


#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

namespace {
using namespace Imath;
using namespace Imf;
using namespace std;
//
// Conversion from raw pixel data to data for the OpenGL frame buffer:
//
//  1) Compensate for fogging by subtracting defog
//     from the raw pixel values.
//
//  2) Multiply the defogged pixel values by
//     2^(exposure + 2.47393).
//
//  3) Values, which are now 1.0, are called "middle gray".
//     If defog and exposure are both set to 0.0, then
//     middle gray corresponds to a raw pixel value of 0.18.
//     In step 6, middle gray values will be mapped to an
//     intensity 3.5 f-stops below the display's maximum
//     intensity.
//
//  4) Apply a knee function.  The knee function has two
//     parameters, kneeLow and kneeHigh.  Pixel values
//     below 2^kneeLow are not changed by the knee
//     function.  Pixel values above kneeLow are lowered
//     according to a logarithmic curve, such that the
//     value 2^kneeHigh is mapped to 2^3.5 (in step 6,
//     this value will be mapped to the the display's
//     maximum intensity).
//
//  5) Gamma-correct the pixel values, assuming that the
//     screen's gamma is 2.2 (or 1 / 0.4545).
//
//  6) Scale the values such that pixels middle gray
//     pixels are mapped to 84.66 (or 3.5 f-stops below
//     the display's maximum intensity).
//
//  7) Clamp the values to [0, 255].
//


float
knee (double x, double f) {
    return float (Imath::Math<double>::log (x * f + 1) / f);
}


float
findKneeF (float x, float y) {
    float f0 = 0;
    float f1 = 1;

    while (knee (x, f1) > y) {
        f0 = f1;
        f1 = f1 * 2;
    }

    for (int i = 0; i < 30; ++i) {
        float f2 = (f0 + f1) / 2;
        float y2 = knee (x, f2);

        if (y2 < y)
            f1 = f2;
        else
            f0 = f2;
    }

    return (f0 + f1) / 2;
}


struct Gamma {
    float g, m, d, kl, f, s, targetBitDepth;
    float multiplier;


    Gamma (float gamma, float exposure, float defog, float kneeLow, float kneeHigh, float targetBD);
    float operator () (half h);
};


Gamma::Gamma (float gamma, float exposure, float defog, float kneeLow, float kneeHigh,float targetBD)
       {
  // printf("%f %f %f %f %f %f\n",gamma, exposure, defog, kneeLow, kneeHigh, targetBD);

       g=(gamma);
       m= (Imath::Math<float>::pow (2, exposure + 2.47393));
        d= (defog);
        kl= (Imath::Math<float>::pow (2, kneeLow));
        f =(findKneeF (Imath::Math<float>::pow (2, kneeHigh) - kl,  Imath::Math<float>::pow (2, 3.5) - kl));
        targetBitDepth=(Imath::Math<float>::pow (2, targetBD)-1);
		s=((Imath::Math<float>::pow (2, targetBD)-1)*Imath::Math<float>::pow (2, -3.5 * g));
   
}


float
Gamma::operator () (half h) {
    //
    // Defog
    //
//return 0;
    float x = max (0.f, (h - d));
	//float x=h;

    //
    // Exposure
    //

    x *= m;

    //
    // Knee
    //

    if (x > kl)
        x = kl + knee (x - kl, f);

    //
    // Gamma
    //

    //x = Imath::Math<float>::pow (x, 0.4545f);
	x = Imath::Math<float>::pow (x, g);

    //
    // Scale and clamp
    //
//     multiplier=84.66f;
//     targetBitDepth=255.f;
    return clamp (x * s, 0.f, targetBitDepth);
}



//
//  Dithering: Reducing the raw 16-bit pixel data to 8 bits for the
//  OpenGL frame buffer can sometimes lead to contouring in smooth
//  color ramps.  Dithering with a simple Bayer pattern eliminates
//  visible contouring.
//

unsigned char
dither (float v, int x, int y) {
    static const float d[4][4] = {
        0.f / 16,  8.f / 16,  2.f / 16, 10.f / 16,
        12.f / 16,  4.f / 16, 14.f / 16,  6.f / 16,
        3.f / 16, 11.f / 16,  1.f / 16,  9.f / 16,
        15.f / 16,  7.f / 16, 13.f / 16,  5.f / 16,
    };

    return (unsigned char) (v + d[y & 3][x & 3]);
}

} // namespace


float gammaConvert8(half h){
  float gamma, exposure, defog, kneeLow, kneeHigh;
  int targetBD=8;
  gamma = 1.0/max(sett.exrGamma,0.00001);
  exposure = sett.exrExposure;
  defog=sett.exrDefog; //defog x fog per color, should be fogR, fogB, fogG
  kneeLow=sett.exrKneeLow; //knee low
  kneeHigh=sett.exrKneeHigh; //knee high
  
        float g, m, d, kl, f, s, targetBitDepth, multiplier;
		g=(gamma);
		m= (Imath::Math<float>::pow (2, exposure + 2.47393));
        d= (defog);
        kl= (Imath::Math<float>::pow (2, kneeLow));
        f =(findKneeF (Imath::Math<float>::pow (2, kneeHigh) - kl,  Imath::Math<float>::pow (2, 3.5) - kl));
        targetBitDepth=(Imath::Math<float>::pow (2, targetBD)-1);
		s=((Imath::Math<float>::pow (2, targetBD)-1)*Imath::Math<float>::pow (2, -3.5 * g));
		
		    //
    // Defog
    //
//return 0;
    float x = max (0.f, (h - d));
	//float x=h;

    //
    // Exposure
    //

    x *= m;

    //
    // Knee
    //

    if (x > kl)
        x = kl + knee (x - kl, f);

    //
    // Gamma
    //

    //x = Imath::Math<float>::pow (x, 0.4545f);
	x = Imath::Math<float>::pow (x, g);

    //
    // Scale and clamp
    //
//     multiplier=84.66f;
//     targetBitDepth=255.f;
    return clamp (x * s, 0.f, targetBitDepth);
}


float gammaConvert16(half h){
  float gamma, exposure, defog, kneeLow, kneeHigh;
  int targetBD=16;
  gamma = 1.0/max(sett.exrGamma,0.00001);
  exposure = sett.exrExposure;
  defog=sett.exrDefog; //defog x fog per color, should be fogR, fogB, fogG
  kneeLow=sett.exrKneeLow; //knee low
  kneeHigh=sett.exrKneeHigh; //knee high
  
        float g, m, d, kl, f, s, targetBitDepth, multiplier;
		g=(gamma);
		m= (Imath::Math<float>::pow (2, exposure + 2.47393));
        d= (defog);
        kl= (Imath::Math<float>::pow (2, kneeLow));
        f =(findKneeF (Imath::Math<float>::pow (2, kneeHigh) - kl,  Imath::Math<float>::pow (2, 3.5) - kl));
        targetBitDepth=(Imath::Math<float>::pow (2, targetBD)-1);
		s=((Imath::Math<float>::pow (2, targetBD)-1)*Imath::Math<float>::pow (2, -3.5 * g));
		
		    //
    // Defog
    //
//return 0;
    float x = max (0.f, (h - d));
	//float x=h;

    //
    // Exposure
    //

    x *= m;

    //
    // Knee
    //

    if (x > kl)
        x = kl + knee (x - kl, f);

    //
    // Gamma
    //

    //x = Imath::Math<float>::pow (x, 0.4545f);
	x = Imath::Math<float>::pow (x, g);

    //
    // Scale and clamp
    //
//     multiplier=84.66f;
//     targetBitDepth=255.f;
    return clamp (x * s, 0.f, targetBitDepth);
}

typedef struct halfRGBA {
    half r;
    half g;
    half b;
    half a;
};

gfcImageLoaderEXR::gfcImageLoaderEXR()
        : gfcImageLoader() {
    theBitmap=NULL;
	pixels=new Imf::Array<Imf::Rgba>;
	halfPixels=new Imf::Array<half>;
}


gfcImageLoaderEXR::~gfcImageLoaderEXR() {}


int gfcImageLoaderEXR::fillProcessor ( gfcImageProcessor& processor ) {
    return 0;
}

char exrCompressionDescriptions[8][60]={"No Compression","Run Lenght Encoding (RLE)","zlib, one scan line at a time compression","zlib, in blocks of 16 scan lines","piz-based wavelet","PXR24 (lossy )","B44,fixed rate (lossy)","B44 (lossy)"};

/*
float gamma(half h, int bpc)
{
	 //
    // Defog
    //
    float d=0;
    float x = (h - d);

    //
    // Exposure
    //

    //float m=1;
    //x *= m;

    //
    // Knee
    //

    float kl=0.0f, f=5.0f;
    if (x > kl)
        x = kl + knee (x - kl, f);

    //
    // Gamma
    //

    x = Imath::Math<float>::pow (x, 0.4545f);

    //
    // Scale and clamp
    //

    float quantum=(1<<bpc)-1;
    float multiplier=quantum/3.5f;
    return Imath::clamp (x * 84.5f, 0.f, quantum);
}*/

void gfcImageLoaderEXR::resampleHalfPixels(int originalW, int originalH, int scale, int &newWidth,int &newHeight){
	Array<half> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/(float)N;
	newHeight=originalH/(float)N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);

			resizedPixels[destination] = (*halfPixels)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}

	//copy to the original array
	halfPixels->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
		(*halfPixels)[i]=resizedPixels[i];
	}
}

void gfcImageLoaderEXR::resamplePixels(int originalW, int originalH, int scale, int &newWidth,int &newHeight)
{
	Array<Rgba> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/N;
	newHeight=originalH/N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);
			
			resizedPixels[destination] = (*pixels)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}
	
	//copy to the original array
	pixels->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
			(*pixels)[i]=resizedPixels[i];
	}

	

}

void gfcImageLoaderEXR::copyPixelsToDisplayWindow(int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset)
{
	Array<Rgba> newPixels;
	
	//copy pixels from the originalPixels array into the new pixels taking only the ones in the display window.
	newPixels.resizeErase(w*h);
	unsigned int counter=max((dy),0)*w;
	for (int i=0;i<copyHeigth;i++) 
	{
		int lineOffset=(startY+i)*dw+startX;
		counter+=preLineOffset;
		for (int j=0;j<copyWidth;j++) 
		{
			newPixels[counter++]=(*pixels)[lineOffset+j];
		}
		counter+=postLineOffset;
	}
	
	//copy to the original array
	pixels->resizeErase(w*h);
	for (int i=w*h-1;i>=0; --i)
	{
		(*pixels)[i]=newPixels[i];
	}

	/*//copy pixels from the originalPixels array into the new pixels taking only the ones in the display window.
	unsigned int counter=0;
	for (int i=0;i<dh;i++) {
		int lineOffset=(y+i)*(dw+x)+x;
		for (int j=0;j<dw;j++) {
			//printf("Getting pixel (%i)\n",lineOffset+j);
			newPixels[counter++] = (*pixels)[lineOffset+j];
		}
	}
	//copy to the original array
	pixels->resizeErase(dw*dh);
	for (int i=dw*dh-1;i>=0; --i)
	{
		(*pixels)[i]=newPixels[i];
	}*/
}
void gfcImageLoaderEXR::copyHalfPixelsToDisplayWindow(int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset)
{
	Array<half> newPixels;

	newPixels.resizeErase(w*h);
	unsigned int counter=max((dy),0)*w;
	for (int i=0;i<copyHeigth;i++) 
	{
		int lineOffset=(startY+i)*dw+startX;
		counter+=preLineOffset;
		for (int j=0;j<copyWidth;j++) 
		{
			newPixels[counter++]=(*halfPixels)[lineOffset+j];
		}
		counter+=postLineOffset;
	}

	//copy to the original array
	halfPixels->resizeErase(w*h);
	for (int i=w*h-1;i>=0; --i)
	{
		(*halfPixels)[i]=newPixels[i];
	}

	/*//copy pixels from the originalPixels array into the new pixels taking only the ones in the display window.
	unsigned int counter=0;
	for (int i=0;i<dh;i++) {
		int lineOffset=(y+i)*(dw+x)+x;
		for (int j=0;j<dw;j++) {
			//printf("Getting pixel (%i)\n",lineOffset+j);
			newPixels[counter++] = (*halfPixels)[lineOffset+j];
		}
	}
	//copy to the original array
	halfPixels->resizeErase(dw*dh);
	for (int i=dw*dh-1;i>=0; --i)
	{

		(*halfPixels)[i]=newPixels[i];
	}*/
}

               
int gfcImageLoaderEXR::layerHasRGBA(Imf::ChannelList::ConstIterator start, Imf::ChannelList::ConstIterator end)
{
		return 0;
}

 //this will return all the channels in a layer, if they contain .R, .G, .B and optionally .A they will be returned in the correct order, not alphabetically.
std::vector<std::string> gfcImageLoaderEXR::getChannelsInLayer(const ChannelList &channels, std::string layerName)
{
	std::vector<std::string> result;
	int hasR, hasG, hasB,hasA, hasOther;
	hasR=hasG=hasB=hasA=hasOther=0;

	Imf::ChannelList::ConstIterator it, end;
	channels.channelsWithPrefix(layerName.c_str(),it,end);
	//printf("Channels In Layer %s:\n",layerName.c_str());
	int counter=0;
	for ( it;it!=end ;it++) {

		std::string theName=it.name();
		std::string componentName=theName;

		int position=theName.find(layerName);
		//printf("position for layer name=%i, size=%i\n",position,theName.size());

		position=theName.find(".R");
		//printf("position=%i, size=%i\n",position,theName.size());
		if (position!=std::string::npos && position==theName.size()-2) 	hasR=true;
		
		position=theName.find(".G");
		//printf("position=%i, size=%i\n",position,theName.size());
		if (position!=std::string::npos && position==theName.size()-2) 	hasG=true;

		position=theName.find(".B");
		//printf("position=%i, size=%i\n",position,theName.size());
		if (position!=std::string::npos && position==theName.size()-2) 	hasB=true;

		position=theName.find(".A");
		//printf("position=%i, size=%i\n",position,theName.size());
		if (position!=std::string::npos && position==theName.size()-2) 	hasA=true;

		result.insert(result.begin(),theName);
		//printf("-->%s\n",theName.c_str());
		counter++;
	}
	
	if (hasR && hasG && hasB && counter<=4)
	{ //THIS MEANS THE CHANNELS ARE RGB, WE WANT THEM TO BE IN RGB ORDER, NOT THE WAY THEY CAME IN, SO RECREATE THE RESULT LIST TO SHOW THIS
		result.clear();
		/*if (hasA)
		{
			result.push_back(layerName+".A");
		}*/
		
			//create a list with RGB
			
		result.push_back(layerName+".B");
			result.push_back(layerName+".G");
			result.push_back(layerName+".R");
			result.push_back(layerName+".A");
					
	}

	return result;
}


int gfcImageLoaderEXR::load ( gfcLoadParams params ) {
    using namespace Imath;
    using namespace Imf;

    //printf ( "Inside EXR Image Loader!\n" );

    bool isLumaChroma=false;
    bool isRGBA=false;

    setGlobalThreadCount(4);
    //get the channel names
    try{
    InputFile file(params.fileName.c_str(),4);
    readMetaData(file.header());
    const ChannelList &channels = file.header().channels();
    
    //we will treat RGB(A) channels as a layer (even if they are not layered), so we detect them.
    //if they are all present (with or without A) we will group them and name that layer RGB(A), that
    //will be the channel name, if one other than A is missing, then we treat them as normal layers.
    //Of course, they can also be selected individually.
    bool hasR=false, hasG=false, hasB=false, hasA=false;
    bool hasY=false, hasRY=false, hasBY=false;
    //1. Get the non layer names, and group them into RGB or RGBA if they exist.
    //we will ignore layered channels, as those will be presented as groups.
    {

        ChannelList::ConstIterator i;

        for (i = channels.begin(); i !=channels.end(); ++i) {

            std::string channelName=i.name();

            i.name();

            if (channelName=="R")
                hasR=true;
            else
                if (channelName=="G")
                    hasG=true;
                else
                    if (channelName=="B")
                        hasB=true;
                    else
                        if (channelName=="A")
                            hasA=true;
                        else
                            if (channelName=="Y")
                                hasY=true;
                            else
                                if (channelName=="RY")
                                    hasRY=true;
                                else
                                    if (channelName=="BY")
                                        hasBY=true;

            //we store the name if it has no periods or the period is at the begining or at the end
            //(according to the EXR docs that means the channels is not part of a layer)
            //printf("*Channel name: %s\n",channelName.c_str());
            int periodPosition=channelName.find('.');
            if (/*true ||*/ periodPosition==std::string::npos ||
                            periodPosition==0 ||
                            periodPosition==(channelName.size()-1)) {
                channelNames.insert(channelNames.begin(),i.name());
            }
        }


    }

    //2. Also find the layered channels, and group them as well.

    std::set<std::string> layerNames;
    channels.layers(layerNames);

    std::set<std::string>::iterator lit=layerNames.begin(),lend=layerNames.end();
    for ( lit;lit!=lend ;lit++ ) {
        channelNames.insert(channelNames.begin(),*lit);
    }

    //detect if we have rgb
    if (hasR && hasB && hasG) {
        std::string tmpRGB="RGB";
        if (hasA)
            tmpRGB+="A";
        channelNames.insert(channelNames.begin(),tmpRGB);
    } else {
        if (hasY && hasRY && hasBY) {
            channelNames.insert(channelNames.begin(),"YRYBY");
        }
    }


    //2.1 Validate channelNames and selected channel

    if (channelNames.empty()) //return if there are no channels
    {
    loadErrorString="EXR file has no channels";
        return 1;
	}

    if (params.channel<0) //this means it's the first time and the user has not selected a channel, default it to the first
        params.channel=0;

	
	Channel theChannels[4]; //4 channels at most.

    //return if asked for a channel out of range.
    if (params.channel > (int)channelNames.size()){
    	loadErrorString="Error loading out of range EXR channel";
        return 1;
        }



    //3. Load the image into a float or half or int buffer
	{
    std::string theChannel=channelNames[params.channel];

    if (theChannel=="YRYBY") {
        isLumaChroma=true;
    } else
        if (theChannel=="RGB" || theChannel=="RGBA") {
            isRGBA=true;
        }

    

    //3.1 Fill a vector with the channelNames that will be used as slices in the in file buffer.
    std::vector<std::string > channelNames; //this contains all the channels that need to be loaded, while theChannel, is the name selected from the GUI
    //theChannel sometimes includes standard names such as RGBA or R, or G or A, but it can also be a layer name or a channel in a layer, so
    //to obtain the list of channels, we try all special cases.
    bool isLayer=layerNames.find(theChannel)!=layerNames.end();

	const Box2i &displayWindow = file.header().displayWindow();
	const Box2i &dataWindow = file.header().dataWindow();
	pixelAspectRatio = file.header().pixelAspectRatio();

	w  = displayWindow.max.x - displayWindow.min.x + 1;
	 h  = displayWindow.max.y - displayWindow.min.y + 1;
	 x = displayWindow.min.x;
	 y = displayWindow.min.y;
	 dw = dataWindow.max.x - dataWindow.min.x + 1;
	 dh = dataWindow.max.y - dataWindow.min.y + 1;
	 dx = dataWindow.min.x;
	 dy = dataWindow.min.y;
	
	/*printf("displayWindow.min.x: %i, displayWindow.min.y: %i\n",displayWindow.min.x,displayWindow.min.y);
	printf("displayWindow:(%i,%i) - (%i,%i)\ndataWindow:(%i,%i) - (%i,%i)\n",displayWindow.min.x,displayWindow.min.y, displayWindow.max.x, displayWindow.max.y,
																			dataWindow.min.x,dataWindow.min.y, dataWindow.max.x, dataWindow.max.y);
	printf("w:%i, h:%i, dw:%i, dh:%i, dx:%i, dy:%i\n",w,h,dw,dh,dx,dy);
	printf("Aspect: %f\n",pixelAspectRatio);*/
    //fileHasAspect=true;
	//aspect=pixelAspectRatio;


	

    if (!isLumaChroma) { //if the image is luma/chroma, then we will read from an rgbInputFile, not from the standard interfase, only the conversion to the last step is true
        //easy case for RGB or RGBA
        if (theChannel=="RGBA" || theChannel=="RGB") {
            channelNames.insert(channelNames.begin(),"A");
            channelNames.insert(channelNames.begin(),"R");
            channelNames.insert(channelNames.begin(),"G");
            channelNames.insert(channelNames.begin(),"B");

        } else {
            if (!isLayer) {	//if the selected channel is not a layer, then just save the name, that is the only channel we will load.
                channelNames.insert(channelNames.begin(),theChannel);
            } else {
                //the channel is a layer, iterate through all the channels in the layer and store them.
                channelNames=this->getChannelsInLayer(channels,theChannel.c_str());
				/*				
				ChannelList::ConstIterator it, end;
                channels.channelsWithPrefix(theChannel.c_str(),it,end);
                int namesCounter=0;
                for ( it;it!=end && namesCounter<4;it++,namesCounter++) {
                    channelNames.insert(channelNames.begin(),it.name());

                }*/
            }
        }

        char typeNames[NUM_PIXELTYPES][10]={"UINT", "HALF", "FLOAT"};
        //3.2 Knowing the name of the slices, find out the types
        //printf("Loading channels:\n");
        numOfComponents=0;
        for ( int i=0; i<channelNames.size(); i++ ) {
            //types[i]=channels.findChannel(channelNames[i].c_str())->type;
            if (channels.findChannel(channelNames[i].c_str())) {
                theChannels[i]=*(channels.findChannel(channelNames[i].c_str()));
               // printf(" *%s (%s) sampling          %i,%i\n",channelNames[i].c_str(),typeNames[theChannels[i].type],theChannels[i].xSampling,theChannels[i].ySampling);
                numOfComponents++;
            }
        }


        //printf("sizeof Rgba %i\n",sizeof(Rgba));
        //3.3 Create the slices for the buffer based on the channel names and info and set to store them in the pixels array.
    
        int sizeOfComponent;
        char *basePointers[4];
        if (numOfComponents>1) {
			//printf("pixels->resizeErase (%i * %i)",dw,dh);
            pixels->resizeErase (dw * dh);
            sizeOfComponent=sizeof(Rgba);
			//printf("-dx-dy*dw: %i\n",-dx-dy*dw);
            basePointers[0]=(char*)&((*pixels)[-dx-dy*dw].r);
            basePointers[1]=(char*)&((*pixels)[-dx-dy*dw].g);
            basePointers[2]=(char*)&((*pixels)[-dx-dy*dw].b);
            basePointers[3]=(char*)&((*pixels)[-dx-dy*dw].a);
        } else {
            halfPixels->resizeErase(dw*dh);
            sizeOfComponent=sizeof(half);
            basePointers[0]=(char*)&((*halfPixels)[-dx-dy*dw]);
        }

	
        FrameBuffer fb;


        for (int i=0;i<channelNames.size() && i<4;i++) {
			//printf("Inserting slice %s\n",channelNames[i].c_str());
            fb.insert(channelNames[i].c_str(), //name
                      Slice(theChannels[i].type, //type of slice (UINT, HALF, FLOAT)
                            basePointers[i], //base (where this slice starts in the memory layout)
                            sizeOfComponent*theChannels[i].xSampling, //stride x
                            sizeOfComponent*theChannels[i].ySampling*dw, //stride y
                            theChannels[i].xSampling, //sampling x
                            theChannels[i].xSampling //sampling y
                           )
                     );
        }
        file.setFrameBuffer(fb);


        //3.4 Read the pixels into the framebuffer
        // NOTE (JEF-16): this file is EXCLUDED from the build (CMakeLists.txt —
        // OpenEXR 3.x migration). The balanceReads read-throttle feature and its
        // machinery (sett.balanceReads, trackManager.readMutex/ioBusy,
        // balanceReadCond) were REMOVED elsewhere; the references below are dead
        // and would not compile if this loader were re-enabled as-is.
		try {
			if (sett.balanceReads)
			{
				std::lock_guard<std::mutex> lock ( trackManager.readMutex );
				while (trackManager.ioBusy!=0)
				{
					balanceReadCond.wait(lock);
				}
				trackManager.ioBusy=1;

				file.readPixels (dataWindow.min.y, dataWindow.max.y);
				
				trackManager.ioBusy=0;
				balanceReadCond.notify_one();
			}
			else
			{
				file.readPixels (dataWindow.min.y, dataWindow.max.y);
			}

        } catch (const std::exception &e) {
            // If some of the pixels in the file cannot be read, warn in some way
            // but still return
            //cerr << e.what() << endl;
            loadErrorString=e.what();
        }
    } 
	else {
		//THE IMAGE IS A LUMA CHROMA IMAGE

        numOfComponents=4;
        RgbaInputFile rgbFile(params.fileName.c_str(),4);
        
        //getMetadata
        readMetaData(rgbFile.header());
                
        pixels->resizeErase(dw*dh);
		//printf("resized the pixels to %ix%i=%i\n",dw,dh,dw*dh);
        rgbFile.setFrameBuffer(&((*pixels)[-dx-dy*dw]),1,dw);
		try {
			if (sett.balanceReads)
			{
				std::lock_guard<std::mutex> lock ( trackManager.readMutex );
				while (trackManager.ioBusy!=0)
				{
					balanceReadCond.wait(lock);
				}
				trackManager.ioBusy=1;

				rgbFile.readPixels (dataWindow.min.y, dataWindow.max.y);

				trackManager.ioBusy=0;
				balanceReadCond.notify_one();
			}
			else
			{
				rgbFile.readPixels (dataWindow.min.y, dataWindow.max.y);
			}

		} catch (const std::exception &e) {
			// If some of the pixels in the file cannot be read, warn in some way
			// but still return
			cerr << e.what() << endl;
			loadErrorString=e.what();
		}
    }
    
	
	dx = dataWindow.min.x - displayWindow.min.x;
	dy = dataWindow.min.y - displayWindow.min.y;

	//ADJUST THE REST OF THE PARAMETERS AFTER LOADING THE PIXELS
	if (sett.exrIgnoreDisplayWindow)
	{
		w = dw;
		h = dh;
		dx = 0;
		dy = 0;
	}
	
	
	}
	//printf("w:%i, h:%i, dw:%i, dh:%i, dx:%i, dy:%i\n",w,h,dw,dh,dx,dy);

	//TO DETERMINE WHAT PORTION OF THE DATA WINDOW WE WILL COPY INTO THE DISPLAY WINDOW, WE FIRST CALCULATE THE INTERSECTING RECTANGLE
	gfcRectang dispW(0,0,w,h);
	gfcRectang dataW(dx,dy,dw,dh);
	gfcRectang intersect=dispW.intersection(dataW);
	//printf("intersect: x%i, y%i w%i, h%i\n",intersect.x, intersect.y, intersect.w, intersect.h);
	int startX=max(0,-dx); //display window has already been centered at 0,0, moving data window with it (unless ignoring display window)
	int startY=max (0,-dy);
	int copyWidth=intersect.w+1;
	int copyHeigth=intersect.h+1;

	//WHERE TO START WRITING TO THE IMAGE BUFFER
	int offset=0;
	offset=max((dy),0)*w*4;

	//HOW MUCH TO PADD AT THE START AND END OF EACH LINE IN THE DISPLAY WINDOW (in case it is bigger and contains the data window)
	int preLineOffset=max((dx),0);
	int postLineOffset=max(((w)-(dx+dw)),0); //distance between the end of the data window to the end of the display window
	//printf("preLineOffset:%i, postLineOffset:%i\n",preLineOffset, postLineOffset);
	preLineOffset*=4;
	postLineOffset*=4;

	//we are downscaling to 8 bits or anything else...
    if (params.compressed!=GFC_16HALF) {
    	loadHALF=false;
        //3.5 Allocate theBitmap if necessary

        int bitSizeForAlloc=8;
        int componentSize=1;
        if (params.compressed==GFC_16BPC) {
            bitSizeForAlloc=16;
            componentSize=2;
        }
        int maxValue=1<<bitSizeForAlloc;
        //theBitmap=gflAllockBitmapEx(GFL_BGRA,dw, dh,bitSizeForAlloc,4,NULL);
		theBitmap=gflAllockBitmapEx(GFL_BGRA,w, h,bitSizeForAlloc,4,NULL);
        bitDepth=bitSizeForAlloc;
	

        //4. Convert and copy the buffer into theBitmap
        {
			
       /*     			halfFunction<float>
           				 rGamma (Gamma::Gamma (1.0/max(sett.exrGamma,0.00001),
				sett.exrExposure,
              				 sett.exrDefog * 1, //defog x fog per color, should be fogR, fogB, fogG
                           			sett.exrKneeLow, //knee low
                          			 sett.exrKneeHigh, //knee high
                           			bitSizeForAlloc),
                   				 -HALF_MAX, HALF_MAX);

			halfFunction<float>
				gGamma (Gamma::Gamma (1.0/max(sett.exrGamma,0.00001),
				sett.exrExposure,
				sett.exrDefog * 1, //defog x fog per color, should be fogR, fogB, fogG
				sett.exrKneeLow, //knee low
				sett.exrKneeHigh, //knee high
				bitSizeForAlloc),
				-HALF_MAX, HALF_MAX);

			halfFunction<float>
				bGamma (Gamma::Gamma (1.0/max(sett.exrGamma,0.00001),
				sett.exrExposure,
				sett.exrDefog * 1, //defog x fog per color, should be fogR, fogB, fogG
				sett.exrKneeLow, //knee low
				sett.exrKneeHigh, //knee high
				bitSizeForAlloc),
				-HALF_MAX, HALF_MAX);*/
            			
			halfFunction<float> halfGammaConvert8(gammaConvert8, -HALF_MAX, HALF_MAX);
			GFL_COLOR color;
			
			
			
            if (numOfComponents>1) {
                
				
				if (bitSizeForAlloc==8) {

                    /*********************************/
                    /*				     */
                    /*	MORE THAN 1 COMPONENT TO 8bit*/
                    /*				     */
                    /********************************/
					//printf("copying from %i,%i for %i,%i\n",startX,startY,copyWidth, copyHeigth);
					if (sett.exrEnableExposureTransformOnLoad)
					{
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
								Rgba *pixel= &(*pixels)[lineOffset+j];
								/*theBitmap->Data[offset++]=rGamma(pixel->r);
								theBitmap->Data[offset++]=gGamma(pixel->g);
								theBitmap->Data[offset++]=bGamma(pixel->b);
								theBitmap->Data[offset++]=rGamma(pixel->a);
								*/
								theBitmap->Data[offset++]=halfGammaConvert8(pixel->r);
								theBitmap->Data[offset++]=halfGammaConvert8(pixel->g);
								theBitmap->Data[offset++]=halfGammaConvert8(pixel->b);
								theBitmap->Data[offset++]=halfGammaConvert8(pixel->a);
				


							}
							offset+=postLineOffset;
						}
					} 
					else
					{
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
								Rgba *pixel= &(*pixels)[lineOffset+j];
								//TODO: change these max and min macros to inline functions, will probably be faster.
								theBitmap->Data[offset++]=max( (min(pixel->r*255,255)),0 );
								theBitmap->Data[offset++]=max( (min(pixel->g*255,255)),0 );
								theBitmap->Data[offset++]=max( (min(pixel->b*255,255)),0 );
								theBitmap->Data[offset++]=max( (min(pixel->a*255,255)),0 );
							}
							offset+=postLineOffset;
						}
					}
					
					
                    
					/*printf("Total pixels to copy:%i, total pixels copied: %i\n",copyWidth*copyHeigth,offset/4);
					printf("copying from %i,%i for %i,%i\n",startX,startY,copyWidth, copyHeigth);*/
                } else {
					/**********************************/
                    /*				      */
                    /*	MORE THAN 1 COMPONENT TO 16bit*/
                    /*				      */
                    /*********************************/
					
					//printf("copying from %i,%i for %i,%i\n",startX,startY,copyWidth, copyHeigth);
					if (sett.exrEnableExposureTransformOnLoad)
					{
						//halfFunction<float> halfGammaConvert16(gammaConvert16, -HALF_MAX, HALF_MAX);
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
								/*Rgba *pixel= &(*pixels)[lineOffset+j];
								(((unsigned short*)(theBitmap->Data))[offset++])=rGamma(pixel->r);
								(((unsigned short*)(theBitmap->Data))[offset++])=bGamma(pixel->g);
								(((unsigned short*)(theBitmap->Data))[offset++])=gGamma(pixel->b);
								(((unsigned short*)(theBitmap->Data))[offset++])=rGamma(pixel->a);*/
								Rgba *pixel= &(*pixels)[lineOffset+j];
								/*(((unsigned short*)(theBitmap->Data))[offset++])=halfGammaConvert16(pixel->r);
								(((unsigned short*)(theBitmap->Data))[offset++])=halfGammaConvert16(pixel->g);
								(((unsigned short*)(theBitmap->Data))[offset++])=halfGammaConvert16(pixel->b);
								(((unsigned short*)(theBitmap->Data))[offset++])=halfGammaConvert16(pixel->a);*/
								
								(((unsigned short*)(theBitmap->Data))[offset++])=gammaConvert16(pixel->r);
								(((unsigned short*)(theBitmap->Data))[offset++])=gammaConvert16(pixel->g);
								(((unsigned short*)(theBitmap->Data))[offset++])=gammaConvert16(pixel->b);
								(((unsigned short*)(theBitmap->Data))[offset++])=gammaConvert16(pixel->a);
							}
							offset+=postLineOffset;
						}
					} 
					else
					{
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
								Rgba *pixel= &(*pixels)[lineOffset+j];
								(((unsigned short*)(theBitmap->Data))[offset++])=max( (min(pixel->r*65532,65532)),0);
								(((unsigned short*)(theBitmap->Data))[offset++])=max( (min(pixel->g*65532,65532)),0);
								(((unsigned short*)(theBitmap->Data))[offset++])=max( (min(pixel->b*65532,65532)),0);
								(((unsigned short*)(theBitmap->Data))[offset++])=max( (min(pixel->a*65532,65532)),0);
							}
							offset+=postLineOffset;
						}
					}
                }
            } else {
                if (bitSizeForAlloc==8) {
                    /*********************************/
                    /*								*/
                    /*	SINGLE COMPONENT TO 8bit     */
                    /*								 */
                    /********************************/
                    
					unsigned char theColor;
					if (sett.exrEnableExposureTransformOnLoad)
					{

						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
								
								/*theBitmap->Data[offset++]=theColor=rGamma((*halfPixels)[lineOffset+j]);
								theBitmap->Data[offset++]=theColor;
								theBitmap->Data[offset++]=theColor;
								theBitmap->Data[offset++]=maxValue;*/
							}
							offset+=postLineOffset;
						}
					} 
					else
					{
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {
									
								theBitmap->Data[offset++]=theColor=theColor=max (min((*halfPixels)[lineOffset+j]*255,255),0);
								theBitmap->Data[offset++]=theColor;
								theBitmap->Data[offset++]=theColor;
								theBitmap->Data[offset++]=maxValue;
							}
							offset+=postLineOffset;
						}
					}

					//printf("copying from %i,%i for %i,%i\n",startX,startY,copyWidth, copyHeigth);
					/*for (int i=0;i<copyHeigth;i++) {
						int lineOffset=(startY+i)*(copyWidth+startX)+startX;
						for (int j=0;j<copyWidth;j++) {
							//printf("Getting pixel (%i)\n",lineOffset+j);
							//Rgba *pixel= &(*pixels)[lineOffset+j];
					

                            color.Red=dither(rGamma((*halfPixels)[lineOffset+j]),j,i);
                            color.Green=color.Red;
                            color.Blue=color.Red;
                            color.Alpha=maxValue;
                            gflSetColorAt(theBitmap,j,i,&color);
                        }
                    }*/
                } else {
                     /*********************************/
                    /*								  */
                    /*	SINGLE COMPONENT TO 16bit     */
                    /*								 */
                    /********************************/
                    unsigned short value;

					unsigned short theColor;
					if (sett.exrEnableExposureTransformOnLoad)
					{

						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {

								/*(((unsigned short*)(theBitmap->Data))[offset++])=theColor=halfGammaConvert16((*halfPixels)[lineOffset+j]);
								(((unsigned short*)(theBitmap->Data))[offset++])=theColor;
								(((unsigned short*)(theBitmap->Data))[offset++])=theColor;
								(((unsigned short*)(theBitmap->Data))[offset++])=maxValue;*/
							}
							offset+=postLineOffset;
						}
					} 
					else
					{
						for (int i=0;i<copyHeigth;i++) {
							int lineOffset=(startY+i)*dw+startX;
							offset+=preLineOffset;
							for (int j=0;j<copyWidth;j++) {

								(((unsigned short*)(theBitmap->Data))[offset++])=theColor=theColor=max(min((*halfPixels)[lineOffset+j]*65532,65532),0);
								(((unsigned short*)(theBitmap->Data))[offset++])=theColor;
								(((unsigned short*)(theBitmap->Data))[offset++])=theColor;
								(((unsigned short*)(theBitmap->Data))[offset++])=maxValue;
							}
							offset+=postLineOffset;
						}
					}
                }
            }
        }
	
        //5. Process the bitmap according to params
	if(params.scale!=100)
		{
			gflResize(theBitmap,NULL,theBitmap->Width*params.scale/100.0,theBitmap->Height*params.scale/100.0,params.filterType==0?GFL_RESIZE_QUICK:GFL_RESIZE_BILINEAR,0);
		}
		
		//crop after scale!
		if(params.crop)
		{
			GFL_RECT cropRect;
			cropRect.x=params.aoi.x;
			cropRect.y=theBitmap->Height-params.aoi.y-params.aoi.h; //we need to invert the y coordinate since the image has a top origin.
			cropRect.w=params.aoi.w;
			cropRect.h=params.aoi.h;
			//printf("cropping to %i %i %i %i\n",cropRect.x,cropRect.y,cropRect.w,cropRect.h);
			gflCrop(theBitmap,NULL,&cropRect);
		}
		
		int resizeToX=theBitmap->Width;
		int resizeToY=theBitmap->Height;
		
		quadSizeX=theBitmap->Width;
		//quadSizeY=theBitmap->Height*1.0/pixelAspect;
		
		quadSizeY=theBitmap->Width/pixelAspectRatio;
		
    } 
	else 
	{

		//LOAD AS HALF FORMAT

        this->loadHALF=1;
        theBitmap=NULL;
		
		if (numOfComponents>1)
		{
			copyPixelsToDisplayWindow(startX, startY,copyWidth, copyHeigth,preLineOffset/4, postLineOffset/4); //we divide by 4 because when we calculate the offsets we think of each 
																												//component in a 4 color pixel,  here we talk about whole pixels
		} 
		else
		{
			copyHalfPixelsToDisplayWindow(startX, startY,copyWidth, copyHeigth,preLineOffset/4, postLineOffset/4);
		}
				
		//resize?
		if(params.scale!=100)
		{
			if (numOfComponents>1)
			{
				resamplePixels(w, h, params.scale, quadSizeX, quadSizeY);
			} 
			else
			{
				resampleHalfPixels(w, h, params.scale, quadSizeX, quadSizeY);
			}
			//resampleFloatPixels(numOfComponents>1?pixels:halfPixels, dw, dh, params.scale, quadSizeX, quadSizeY); 
		}
		else
		{
			quadSizeX=w;
			quadSizeY=h;
		}
    }

    //6. fill required frame information
    switch ( params.compressed ) {

    case GFC_4BPC:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE4;
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGB4;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGBA4;
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE4;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_8BPC:

        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGB8;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGBA8;
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_16BPC:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;

            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGB16F_ARB;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            break;
        default:
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_16HALF:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_HALF_FLOAT_ARB;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;
            frameInfo.dataPointer=&(*halfPixels)[0];
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_HALF_FLOAT_ARB;
            frameInfo.internalFormat=GL_RGB16F_ARB;
            frameInfo.dataPointer=&(*pixels)[0];
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_HALF_FLOAT_ARB;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            frameInfo.dataPointer=&(*pixels)[0];
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_HALF_FLOAT_ARB;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            frameInfo.dataPointer=&(*pixels)[0];
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        texCoords.x=0;
        texCoords.y=0;
		
        texCoords.w=quadSizeX;
        texCoords.h=quadSizeY;
        break;

    case GFC_S3TCDX1:

        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            break;
        default:
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        }

		//resize to the next divisible by 4

		int resizeToX=getNextDivisibleBy4(theBitmap->Width);
		int resizeToY=getNextDivisibleBy4(theBitmap->Height);
		GFL_COLOR gfl_color;
		GFL_ERROR error;
		gfl_color.Red=gfl_color.Blue=gfl_color.Green=0.2;

		texCoords.x=0;
		texCoords.y=0;
		texCoords.w=theBitmap->Width/(float)resizeToX;
		texCoords.h=theBitmap->Height/(float)resizeToY;

		quadSizeX=theBitmap->Width;
		quadSizeY=theBitmap->Height;

		if (resizeToX!=theBitmap->Width || resizeToY!=theBitmap->Height) {
			//printf("Resizing canvas to %ix%i\n",resizeToX,resizeToY);
			error=gflResizeCanvas(theBitmap,NULL,resizeToX,resizeToY,GFL_CANVASRESIZE_TOPLEFT,&gfl_color);
			if (error) {
				printf("ResizeCanvas error: %s\n",gflGetErrorString(error));
			}
		}

        frameInfo.target=GL_TEXTURE_2D;
        texCoords.x=0;
        frameInfo.dataPointer=theBitmap->Data;
        /*texCoords.y=0;
        texCoords.w=1;
        texCoords.h=1;*/
        break;
    }

    //special case for LumaChroma images, always set the format to GL_RGBA instead of GL_BGRA or whatever, since we have no control over component swizzling.
    if (isLumaChroma)
        frameInfo.format=GL_RGBA;

    int bitDepths[NUM_PIXELTYPES]={32/*UINT*/, 16/*"HALF"*/, 32/*"FLOAT"*/};
    originalBitDepth=bitDepths[theChannels[0].type];
    originalNumOfComponents=channelNames.size();

    sizeX=loadHALF?quadSizeX:theBitmap->Width;
    sizeY=loadHALF?quadSizeY:theBitmap->Height;
	
    quadSizeX=loadHALF?quadSizeX:theBitmap->Width;
	quadSizeY=loadHALF?quadSizeY:theBitmap->Height;
	if (!sett.exrIgnoreHeadersAspectRatio)
	{
		quadSizeX*=pixelAspectRatio; //this stretches horizontally
		//quadSizeY/=pixelAspectRatio; //this squashes vertically, should we make it configurable? which is prefered or more natural?
	}
	
	
    
	
	//printf("quadSizeX=%i quadSizeY=%i\n",quadSizeX, quadSizeY);

    format = "EXR";
    formatDescription="ILM OpenEXR v";
    formatDescription += '0'+file.version();
    
    compressionDescription= exrCompressionDescriptions[file.header().compression()];

}
    catch(const std::exception &e){
    	//fl_alert(e);
   	loadErrorString=std::string("EXR exception: ")+e.what();
    	return 1;
    }
    return 0;
}

int gfcImageLoaderEXR::peek ( gfcLoadParams params, gfcPeekInfo* results ) {
    return 0;
}

void* gfcImageLoaderEXR::getPixelPointer() {
    if (this->loadHALF) {
        return (void*)&pixels[0];
    } else {
        return theBitmap->Data;
    }
}



std::vector< std::string > gfcImageLoaderEXR::getChannelNames() {
    return channelNames;
}

void gfcImageLoaderEXR::releaseMemory() {
    if (theBitmap)
        gflFreeBitmap(theBitmap);

    theBitmap=NULL;

    pixels->resizeErase(0);
    halfPixels->resizeErase(0);
}

void gfcImageLoaderEXR::readMetaData(const Imf::Header & header)
{
	using namespace Imf;
	std::stringstream ss;
	
	std::multimap<std::string,std::string>::iterator it=metaData.begin();
        it=metaData.insert(it,std::pair<std::string,std::string>("Additional Comments (comments)",hasComments(header)?comments(header):""));
        it=metaData.insert(it,std::pair<std::string,std::string>("Image Owner (owner)",hasOwner(header)?owner(header):""));
        it=metaData.insert(it,std::pair<std::string,std::string>("Capture Date (capDate)",hasCapDate(header)?capDate(header):""));
        
        if(hasFocus(header))
        {
        	ss.str("");
        	ss<<focus(header);
        	ss<<"m";
        	it=metaData.insert(it,std::pair<std::string,std::string>("Focus Distance (focus)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("Focus Distance (focus)",""));
        }
        
        if(hasExpTime(header))
        {
        	ss.str("");
        	ss<<expTime(header);
        	ss<<"s";
        	it=metaData.insert(it,std::pair<std::string,std::string>("Exposure Time (expTime)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("Exposure Time (expTime)",""));
        }
        
        if(hasAperture(header))
        {
        	ss.str("");
        	ss<<aperture(header);
        	ss<<" f-stops";
        	it=metaData.insert(it,std::pair<std::string,std::string>("Lens Aperture (aperture)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("Lens Aperture (aperture)",""));
        }
        
        if(hasIsoSpeed(header))
        {
        	ss.str("");
        	ss<<isoSpeed(header);
        	it=metaData.insert(it,std::pair<std::string,std::string>("ISO Speed (isoSpeed)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("ISO Speed (isoSpeed)",""));
        }
        
        if(hasKeyCode(header))
        {
        	ss.str("");
        	Imf::KeyCode kc= keyCode(header);
        	ss<<kc.filmMfcCode();
        	ss<<" ";
        	ss<<kc.filmType();
        	ss<<" ";
        	ss<<kc.prefix();
        	ss<<" ";
        	ss<<kc.count();
        	ss<<" ";
        	ss<<kc.perfOffset();
        	ss<<" ";
        	ss<<kc.perfsPerCount();
        	ss<<" ";
        	it=metaData.insert(it,std::pair<std::string,std::string>("KeyCode (keyCode)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("KeyCode (keyCode)",""));
        }
        
        if(hasTimeCode(header))
        {
        	ss.str("");
        	TimeCode tc=timeCode(header);
        	ss<< tc.hours();
        	ss<< ":";
        	ss<< tc.minutes();
        	ss<< ":";
        	ss<< tc.seconds();
        	ss<< ":";
        	ss<< tc.frame();
        	ss<< ":";
        	
        	it=metaData.insert(it,std::pair<std::string,std::string>("TimeCode (timeCode)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("TimeCode (timeCode)",""));
        }
        
        if(hasLongitude(header) && hasLatitude(header) && hasAltitude(header))
        {
        	ss.str("");
        	ss<< longitude(header);
        	ss<< ", ";
        	
        	ss<< latitude(header);
        	ss<< ", ";
        	
        	ss<< altitude(header);
        	
        	
        	it=metaData.insert(it,std::pair<std::string,std::string>("Location (longitude, latitude, altitude)",ss.str()));
        }
        else
        {
        	it=metaData.insert(it,std::pair<std::string,std::string>("Location (longitude, latitude, altitude)",""));
        }
        
        
        
}
