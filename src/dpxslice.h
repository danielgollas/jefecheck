#ifndef DPXSLICE_H
#define DPXSLICE_H



/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

#include <string>
#include "gfcStructures.h"

/*
  Packing methods for filled words.
*/

#define IS_UNDEFINED_U8(value) (value == ((unsigned char) ~0))
#define IS_UNDEFINED_U16(value) (value == ((unsigned short) ~0))
#define IS_UNDEFINED_U32(value) (value == ((unsigned int) ~0))
#define IS_UNDEFINED_R32(value) (*((unsigned int *) &value) == ((unsigned int) ~0))
#define IS_UNDEFINED_ASCII(value) (!(value[0] > 0))

enum EndianType
{
 MSBEndian,
 LSBEndian
};

enum DPXImageElementDescriptor
{
    ImageElementUnspecified=0,
    ImageElementRed=1,
    ImageElementGreen=2,
    ImageElementBlue=3,
    ImageElementAlpha=4,
    ImageElementLuma=6,
    ImageElementColorDifferenceCbCr=7,
    ImageElementDepth=8,
    ImageElementCompositeVideo=9,
    ImageElementRGB=50,              /* BGR order */
    ImageElementRGBA=51,             /* BGRA order */
    ImageElementABGR=52,             /* ARGB order */
    ImageElementCbYCrY422=100,       /* SMPTE 125M, 4:2:2 */
    ImageElementCbYACrYA4224=101,    /* 4:2:2:4 */
    ImageElementCbYCr444=102,        /* 4:4:4 */
    ImageElementCbYCrA4444=103,      /* 4:4:4:4 */
    ImageElementUserDef2Element=150, /* User-defined 2-component element */
    ImageElementUserDef3Element=151, /* User-defined 3-component element */
    ImageElementUserDef4Element=152, /* User-defined 4-component element */
    ImageElementUserDef5Element=153, /* User-defined 5-component element */
    ImageElementUserDef6Element=154, /* User-defined 6-component element */
    ImageElementUserDef7Element=155, /* User-defined 7-component element */
    ImageElementUserDef8Element=156  /* User-defined 8-component element */
} ;

/*
  Transfer characteristic enumerations.
*/
typedef enum
{
    TransferCharacteristicUserDefined=0,
    TransferCharacteristicPrintingDensity=1,
    TransferCharacteristicLinear=2,
    TransferCharacteristicLogarithmic=3,
    TransferCharacteristicUnspecifiedVideo=4,
    TransferCharacteristicSMTPE274M=5,     /* 1920x1080 TV */
    TransferCharacteristicITU_R709=6,      /* ITU R709 */
    TransferCharacteristicITU_R601_625L=7, /* 625 Line */
    TransferCharacteristicITU_R601_525L=8, /* 525 Line */
    TransferCharacteristicNTSCCompositeVideo=9,
    TransferCharacteristicPALCompositeVideo=10,
    TransferCharacteristicZDepthLinear=11,
    TransferCharacteristicZDepthHomogeneous=12
} DPXTransferCharacteristic;

/*
  Colorimetric enumerations.
*/
typedef enum
{
    ColorimetricUserDefined=0,       /* User defined */
    ColorimetricPrintingDensity=1,   /* Printing density */
    ColorimetricLinear=2,            /* Linear */
    ColorimetricLogarithmic=3,       /* Logarithmic */
    ColorimetricUnspecifiedVideo=4,
    ColorimetricSMTPE274M=5,         /* 1920x1080 TV */
    ColorimetricITU_R709=6,          /* ITU R709 */
    ColorimetricITU_R601_625L=7,     /* 625 Line ITU R601-5 B & G */
    ColorimetricITU_R601_525L=8,     /* 525 Line ITU R601-5 M */
    ColorimetricNTSCCompositeVideo=9,
    ColorimetricPALCompositeVideo=10,
    ColorimetricZDepthLinear=11,
    ColorimetricZDepthHomogeneous=12
} DPXColorimetric;

/*
  Packing methods for filled words.
*/
typedef enum
{
    PackingMethodPacked=0,           /* Packed with no padding */
    PackingMethodWordsFillLSB=1,     /* Method 'A', padding bits in LSB of 32-bit word */
    PackingMethodWordsFillMSB=2      /* Method 'B', padding bits in MSB of 32-bit word (deprecated) */
}  ImageComponentPackingMethod;

struct dpx_file_information
{
    unsigned int  magic_num;        /* magic number 0x53445058 (SDPX) or 0x58504453 (XPDS) */
    unsigned int  offset;           /* offset to image data in chars */
    char vers[8];          /* which header format version is being used (v1.0)*/
    unsigned int   file_size;        /* file size in chars */
    unsigned int  ditto_key;        /* read time short cut - 0 = same, 1 = new */
    unsigned int  gen_hdr_size;     /* generic header length in chars */
    unsigned int  ind_hdr_size;     /* industry header length in chars */
    unsigned int  user_data_size;   /* user-defined data length in chars */
    char file_name[100];   /* iamge file name */
    char create_time[24];  /* file creation date "yyyy:mm:dd:hh:mm:ss:LTZ" */
    char creator[100];     /* file creator's name */
    char project[200];     /* project name */
    char copyright[200];   /* right to use or copyright info */
    unsigned int  key;              /* encryption ( FFFFFFFF = unencrypted ) */
    char Reserved[104];    /* reserved field TBD (need to pad) */
};
struct dpx_image_element
    {
        unsigned int   data_sign;        /* data sign (0 = unsigned, 1 = signed ) */
				 /* "Core set images are unsigned" */
        unsigned int   ref_low_data;     /* reference low data code value */
        float ref_low_quantity; /* reference low quantity represented */
        unsigned int   ref_high_data;    /* reference high data code value */
        float    ref_high_quantity;/* reference high quantity represented */
        unsigned char     descriptor;       /* descriptor for image element */
        unsigned char     transfer;         /* transfer characteristics for element */
        unsigned char     colorimetric;     /* colormetric specification for element */
        unsigned char     bit_size;         /* bit size for element */
	unsigned short    packing;          /* packing for element */
        unsigned short    encoding;         /* encoding for element */
        unsigned int   data_offset;      /* offset to data of element */
        unsigned int   eol_padding;      /* end of line padding used in element */
        unsigned  int  eo_image_padding; /* end of image padding used in element */
        unsigned char  description[32];  /* description of element */
    };

struct dpx_image_information
{
    unsigned short    orientation;          /* image orientation */
    unsigned short    element_number;       /* number of image elements */
    unsigned int   pixels_per_line;      /* or x value */
    unsigned int   lines_per_image_ele;  /* or y value, per element */
    dpx_image_element image_element[8];          /* NOTE THERE ARE EIGHT OF THESE */

    char reserved[52];             /* reserved for future use (padding) */
};

struct dpx_image_orientation
{
    unsigned int  x_offset;               /* X offset */
    unsigned int  y_offset;               /* Y offset */
    float   x_center;               /* X center */
    float   y_center;               /* Y center */
    unsigned int  x_orig_size;            /* X original size */
    unsigned int  y_orig_size;            /* Y original size */
    char file_name[100];         /* source image file name */
    char creation_time[24];      /* source image creation date and time */
    char input_dev[32];          /* input device name */
    char input_serial[32];       /* input device serial number */
    unsigned short   border[4];              /* border validity (XL, XR, YT, YB) */
    unsigned int  pixel_aspect[2];        /* pixel aspect ratio (H:V) */
    float   x_scanned_size;            /* X scanned size */
    float   y_scanned_size;            /* Y scanned size */
    unsigned char    reserved[20];           /* reserved for future use (padding) */
};


struct dpx_motion_picture_film_header
{
    char film_mfg_id[2];    /* film manufacturer ID code (2 digits from film edge code) */
    char film_type[2];      /* file type (2 digits from film edge code) */
    char offset[2];         /* offset in perfs (2 digits from film edge code)*/
    char prefix[6];         /* prefix (6 digits from film edge code) */
    char count[4];          /* count (4 digits from film edge code)*/
    char format[32];        /* format (i.e. academy) */
    unsigned int   frame_position;    /* frame position in sequence */
    unsigned int   sequence_len;      /* sequence length in frames */
    unsigned int   held_count;        /* held count (1 = default) */
    float   frame_rate;        /* frame rate of original in frames/sec */
    float   shutter_angle;     /* shutter angle of camera in degrees */
    char frame_id[32];      /* frame identification (i.e. keyframe) */
    char slate_info[100];   /* slate information */
    char    reserved[56];      /* reserved for future use (padding) */
};


struct dpx_television_header
{
    unsigned int tim_code;            /* SMPTE time code */
    unsigned int userBits;            /* SMPTE user bits */
    unsigned char  interlace;           /* interlace ( 0 = noninterlaced, 1 = 2:1 interlace*/
    unsigned char  field_num;           /* field number */
    unsigned char  video_signal;        /* video signal standard (table 4)*/
    unsigned char  unused;              /* used for char alignment only */
    float hor_sample_rate;     /* horizontal sampling rate in Hz */
    float ver_sample_rate;     /* vertical sampling rate in Hz */
    float frame_rate;          /* temporal sampling rate or frame rate in Hz */
    float time_offset;         /* time offset from sync to first pixel */
    float gamma;               /* gamma value */
    float black_level;         /* black level code value */
    float black_gain;          /* black gain */
    float break_point;         /* breakpoint */
    float white_level;         /* reference white level code value */
    float integration_times;   /* integration time(s) */
    char  reserved[76];        /* reserved for future use (padding) */
};

struct dpx_UserInfo
{
  char
    id[32];
};

typedef unsigned char BYTE4 [4];
struct dpx_image_data_element
{
	BYTE4 *Data;
};



class gfcDPXMetaData
{
	//general 
	std::string copyright;
	std::string create_time;
	std::string creator;
	std::string file_name;
	
	//film
	int count;
	std::string film_mfg_id;
	std::string film_type;
	std::string format;
	std::string frame_id;
	float frame_rate;
	unsigned int held_count;
	std::string offset;
	unsigned int sequence_len;
	float shutter_angle;
	std::string slate_info;
	
	//tv
    unsigned int tim_code;            /* SMPTE time code */
    unsigned int userBits;            /* SMPTE user bits */
    unsigned char  interlace;           /* interlace ( 0 = noninterlaced, 1 = 2:1 interlace*/
    unsigned char  field_num;           /* field number */
    unsigned char  video_signal;        /* video signal standard (table 4)*/
    unsigned char  unused;              /* used for char alignment only */
    float hor_sample_rate;     /* horizontal sampling rate in Hz */
    float ver_sample_rate;     /* vertical sampling rate in Hz */
//    float frame_rate;          /* temporal sampling rate or frame rate in Hz */
    float time_offset;         /* time offset from sync to first pixel */
    float gamma;               /* gamma value */
    float black_level;         /* black level code value */
    float black_gain;          /* black gain */
    float break_point;         /* breakpoint */
    float white_level;         /* reference white level code value */
    float integration_times;   /* integration time(s) */
   
	
		
};

class DpxSlice{
public:
    DpxSlice();
   
    ~DpxSlice();
    //overload the assignment operator
    const DpxSlice &operator=(const DpxSlice &slice);
    char fileName[300];
    int readHeader(const char *fileName);
    
    int readSlice(long startOffset, long numLines);
    int releasePixelMemory();
    void printInfo(bool extensive=0);
    unsigned int samplesPerPixel();
    pixelRGB16Bit *rgb16Pixels;
    pixelRGB8Bit *rgb8Pixels;
    pixelRGBA8Bit *rgba8Pixels;
    dpx_file_information fileInfo;
    dpx_image_information imageInfo;
    dpx_image_orientation imageOrientation;
    dpx_motion_picture_film_header motionHeader;
    dpx_television_header tvHeader;
    dpx_UserInfo userInfo;
    char timeCodeString[11];
    bool swab;
};

void
swabUInt16(unsigned short* wp);
void
swabUInt32(unsigned int* lp);
void
swabArrayOfUInt16(unsigned short* wp, size_t n);
void
swabArrayOfUInt32(unsigned int* lp, size_t n);
void
swabFloat(float *fp);
void
swabArrayOfFloat(float *fp, size_t n);
void
swabDouble(double *dp);
void
swabArrayOfDouble(double* dp, size_t n);

#endif
