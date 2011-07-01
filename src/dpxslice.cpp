#include "dpxslice.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include <algorithm> //required for std::swap
#include <assert.h>
#include "lut1d.h"
#include "gfcStructures.h"
#include "string.h"
#define ByteSwap5(x) ByteSwap((unsigned char *) &x,sizeof(x))

extern LUT1D testLut;



static void ReadRowSamples(const unsigned char *scanline,
                           const unsigned int samples_per_row,
                           const unsigned int bits_per_sample,
                           const unsigned int packing_method,
                           const unsigned int endian_type,
                           const bool swap_word_datums,
                           unsigned int *samples) {
    register unsigned long
    i;

    register sample_t
    *sp;

    register unsigned int
    sample;

    sp=samples;
    if ((packing_method != PackingMethodPacked) &&
            ((bits_per_sample == 10) || (bits_per_sample == 12))) {
        bool
        word_pad_lsb=false,
                     word_pad_msb=false;

        if (packing_method == PackingMethodWordsFillLSB)
            word_pad_lsb=true;
        else if (packing_method == PackingMethodWordsFillMSB)
            word_pad_msb=true;

        if (bits_per_sample == 10) {
            register PackedU32Word
            packed_u32;

            register unsigned int
            datum;

            unsigned int
            shifts[3] = { 0, 0, 0 };

            if (word_pad_lsb) {
                /*
                  Padding in LSB (Method A)  Standard method.
                */
                if (swap_word_datums == false) {
                    shifts[0]=2;  /* datum-0 / blue */
                    shifts[1]=12; /* datum-1 / green */
                    shifts[2]=22; /* datum-2 / red */
                } else {
                    shifts[0]=22; /* datum-2 / red */
                    shifts[1]=12; /* datum-1 / green */
                    shifts[2]=2;  /* datum-0 / blue */
                }
            } else if (word_pad_msb) {
                /*
                  Padding in MSB (Method B)  Deprecated method.
                */
                if (swap_word_datums == false) {
                    shifts[0]=0;  /* datum-0 / blue */
                    shifts[1]=10; /* datum-1 / green */
                    shifts[2]=20; /* datum-2 / red */

//                     shifts[0]=2;  /* datum-0 / blue */
//                     shifts[1]=12; /* datum-1 / green */
//                     shifts[2]=22; /* datum-2 / red */
                } else {
                    shifts[0]=20; /* datum-2 / red */
                    shifts[1]=10; /* datum-1 / green */
                    shifts[2]=0;  /* datum-0 / blue */

//                     shifts[0]=22; /* datum-2 / red */
//                     shifts[1]=12; /* datum-1 / green */
//                     shifts[2]=2;  /* datum-0 / blue */
                }
            }

            if (endian_type == MSBEndian) {
                for (i=samples_per_row/3; i > 0; --i) {
                    //for each element in the 32bit word, extract the actual 10 bit value, by shifting the right amount and taking the last 10 bits (0x3FF)
                    //datum=0;
                    MSBOctetsToPackedU32Word(scanline,packed_u32);
                    *sp++=(packed_u32.word >> shifts[0]) & 0x3FF;
                    *sp++=(packed_u32.word >> shifts[1]) & 0x3FF;
                    *sp++=(packed_u32.word >> shifts[2]) & 0x3FF;//*/

//                      	*sp++=((packed_u32.word >> shifts[0]) & 0x3FF)>>2;
//                   	*sp++=((packed_u32.word >> shifts[1]) & 0x3FF)>>2;
//                   	*sp++=((packed_u32.word >> shifts[2]) & 0x3FF)>>2;

                    /* *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)*0.24;
                     *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)*0.24;
                     *sp++=((packed_u32.word >> shifts[datum]) & 0x3FF)*0.24;*/


//                     *sp++=((packed_u32.word >> shifts[datum++]) & 0xFF);
//                     *sp++=((packed_u32.word >> shifts[datum++]) & 0xFF);
//                     *sp++=((packed_u32.word >> shifts[datum]) & 0xFF);

//                     *sp++=((packed_u32.word >> shifts[0]) & 0x3FF);
//                     *sp++=((packed_u32.word >> shifts[1]) & 0x3FF);
//                     *sp++=((packed_u32.word >> shifts[2]) & 0x3FF);


                }
                if ((samples_per_row % 3)) {
                    datum=0;
                    MSBOctetsToPackedU32Word(scanline,packed_u32);
                    for (i=(samples_per_row % 3); i > 0; --i)
                        *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF);
                }
            } else if (endian_type == LSBEndian) {
                for (i=samples_per_row/3; i > 0; --i) {
                    datum=0;
                    LSBOctetsToPackedU32Word(scanline,packed_u32);
                    *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                }
                if ((samples_per_row % 3)) {
                    datum=0;
                    LSBOctetsToPackedU32Word(scanline,packed_u32);
                    for (i=(samples_per_row % 3); i > 0; --i)
                        *sp++=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                }
            }
            return;
        } else if (bits_per_sample == 12) {
            if (word_pad_lsb) {
                /*
                  Padding in LSB (Method A)  Standard method.
                */
                if (endian_type == MSBEndian) {
                    for (i=samples_per_row; i > 0; i--) {
                        sample=0;
                        sample |= (*scanline++ << 8);
                        sample |= (*scanline++);
                        sample >>= 4;
                        *sp++=sample;
                    }
                } else if (endian_type == LSBEndian) {
                    for (i=samples_per_row; i > 0; i--) {
                        sample=0;
                        sample |= (*scanline++);
                        sample |= (*scanline++ << 8);
                        sample >>= 4;
                        *sp++=sample;
                    }
                }
                return;
            } else if (word_pad_msb) {
                /*
                  Padding in MSB (Method B)  Deprecated method.
                */
                if (endian_type == MSBEndian) {
                    for (i=samples_per_row; i > 0; i--) {
                        sample=0;
                        sample |= (*scanline++ << 8);
                        sample |= (*scanline++);
                        sample &= 0xFFF;
                        *sp++=sample;
                    }
                } else if (endian_type == LSBEndian) {
                    for (i=samples_per_row; i > 0; i--) {
                        sample=0;
                        sample |= (*scanline++);
                        sample |= (*scanline++ << 8);
                        sample &= 0xFFF;
                        *sp++=sample;
                    }
                }
                return;
            }
        }
    }

    /*
      Special fast handling for 8-bit images.
    */
    if (bits_per_sample == 8) {
        for (i=samples_per_row; i > 0; i--)
            *sp++= (sample_t) *scanline++;
        return;
    }

    /*
      Special fast handling for 16-bit images.
    */
    if (bits_per_sample == 16) {
        if (endian_type == MSBEndian) {
            for (i=samples_per_row; i > 0; i--) {
                sample=0;
                sample |= (*scanline++ << 8);
                sample |= (*scanline++);
                *sp++=sample;
            }
        } else if (endian_type == LSBEndian) {
            for (i=samples_per_row; i > 0; i--) {
                sample=0;
                sample |= (*scanline++);
                sample |= (*scanline++ << 8);
                *sp++=sample;
            }
        }
        return;
    }

    /*
      Special fast handling for 32-bit (float) images.
    */
    if (bits_per_sample == 32) {
        register PackedU32Word
        packed_u32;

        if (endian_type == MSBEndian) {
            for (i=samples_per_row; i > 0; i--) {
                MSBOctetsToPackedU32Word(scanline,packed_u32);
                *sp++=packed_u32.word;
            }
        } else if (endian_type == LSBEndian) {
            for (i=samples_per_row; i > 0; i--) {
                LSBOctetsToPackedU32Word(scanline,packed_u32);
                *sp++=packed_u32.word;
            }
        }
        return;
    }
}

static size_t DPXRowBytes(const unsigned long rows,
                          const unsigned int samples_per_row,
                          const unsigned int bits_per_sample,
                          const unsigned int packing_method) {
    size_t
    octets = 0;

    switch (bits_per_sample) {
    case 1:
        /* Packed 1-bit samples in 32-bit words. Rows are padded out to 32-bit alignment */
        octets=rows*((samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        break;
    case 8:
        /* C.1 8-bit samples in a 32-bit word. Rows are padded out to 32-bit alignment */
        octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        break;
    case 32:
        /* 32-bit samples in a 32-bit word */
        octets=samples_per_row*sizeof(unsigned int)*rows;
        break;
    case 10:
        if ((packing_method == PackingMethodWordsFillLSB) ||
                (packing_method == PackingMethodWordsFillMSB)) {
            /* C.3 Three 10-bit samples per 32-bit word */
            octets=(((( (rows*samples_per_row+2)/3)*sizeof(unsigned int)*8)+31)/32)*sizeof(unsigned int)-samples_per_row%2;
        } else {
            /* C.2 Packed 10-bit samples in a 32-bit word. */
            octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int)-samples_per_row%2;
        }
        break;
    case 12:
        if ((packing_method == PackingMethodWordsFillLSB) ||
                (packing_method == PackingMethodWordsFillMSB)) {
            /* C.5: One 12-bit sample per 16-bit word */
            octets=((( rows*samples_per_row*sizeof(unsigned short)*8)+15)/16)*sizeof(unsigned short);
        } else {
            /* C.4: Packed 12-bit samples in a 32-bit word. */
            octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        }
        break;
    case 16:
        /* C.6 16-bit samples in 16-bit words. */
        octets=((( rows*samples_per_row*bits_per_sample)+15)/16)*sizeof(unsigned short);
        break;
    case 64:
        /* 64-bit samples in 64-bit words. */
        octets= rows*samples_per_row*8;
        break;
    }

    return octets;
}

DpxSlice::DpxSlice() {}


DpxSlice::~DpxSlice() {}


/**
 * Read a DPX Header
 * @param fileName
 * @param fileInfo
 * @param imageInfo
 * @param imageOrientation
 * @param filmHeader
 * @param tvHeader
 * @return
 */

static void SMPTEBitsToString(const unsigned int value, char *str) {
    unsigned int
    pos,
    shift = 28;

    for (pos=8; pos > 0; pos--, shift -= 4) {
        sprintf(str,"%01u",(unsigned int) ((value >> shift) & 0x0fU));
        str += 1;
        if ((pos > 2) && (pos % 2)) {
            strcat(str,":");
            str++;
        }
    }
    *str='\0';
}

const DpxSlice& DpxSlice::operator=(const DpxSlice &slice) {
    memcpy(&fileInfo,&slice.fileInfo,sizeof(dpx_file_information));
    memcpy(&imageInfo,&slice.imageInfo,sizeof(dpx_image_information));
    memcpy(&imageOrientation,&slice.imageOrientation,sizeof(dpx_image_orientation));
    memcpy(&motionHeader,&slice.motionHeader,sizeof(dpx_motion_picture_film_header));
    memcpy(&tvHeader,&slice.tvHeader,sizeof(dpx_television_header));
    memcpy(&userInfo,&slice.userInfo,sizeof(dpx_UserInfo));
    strncpy(timeCodeString,slice.timeCodeString,11);

    return slice;
}

void DpxSlice::printInfo( bool extensive) {


    printf("\n***********************************\n%s",fileInfo.file_name);
    printf("\n***********************************\n");
    std::cout<<"\tFILE INFO:"<<std::endl;
    if (fileInfo.magic_num==0x53445058)
        printf("Magic Number Hex:0x%X (SDPX)\n",fileInfo.magic_num);
    if (fileInfo.magic_num==0x58504453)
        printf("Magic Number Hex:0x%X (XPDS)\n",fileInfo.magic_num);
    std::string testString;
    //printf("Magic Number Hex:0x%X\n",fileInfo.magic_num);
    std::cout<<"Header Version:"<<fileInfo.vers<<std::endl;
    std::cout<<"Image File Name:"<<fileInfo.file_name<<std::endl;
    std::cout<<"Creation Time:"<<fileInfo.create_time<<std::endl;
    std::cout<<"Creator:"<<fileInfo.creator<<std::endl;
    std::cout<<"Project:"<<fileInfo.project<<std::endl;
    std::cout<<"Copyright:"<<fileInfo.copyright<<std::endl;
    std::cout<<"Generic Header Size:"<<fileInfo.gen_hdr_size<<std::endl;
    std::cout<<"Industry Header Size:"<<fileInfo.ind_hdr_size<<std::endl;
    std::cout<<"File Size (MB/KB/B):"<<fileInfo.file_size/1024.0/1024.0<<"/"<<fileInfo.file_size/1024.0<<"/"<<fileInfo.file_size<<std::endl;
    std::cout<<"----------------------------------"<<std::endl;
    std::cout<<"\tIMAGE INFO:"<<std::endl;
    std::cout<<"Num. of Elements:"<<imageInfo.element_number<<std::endl;
    std::cout<<"Pixels per line (width):"<<(imageInfo.pixels_per_line)<<std::endl;
    std::cout<<"Lines per element (height):"<<imageInfo.lines_per_image_ele<<std::endl;

    // imageInfo
    //imageInfo.image_element[0].bit_size=12;
    std::cout<<"Bits per sample:"<<(short)imageInfo.image_element[0].bit_size<<std::endl;

    std::cout<<"Samples per pixel:"<<samplesPerPixel()<<std::endl;
    std::cout<<"Packing:"<<imageInfo.image_element[0].packing<<std::endl;
    std::cout<<"Description:"<<imageInfo.image_element[0].description<<std::endl;

    std::cout<<"----------------------------------"<<std::endl;
    std::cout<<"\tORIENTATION INFO:"<<std::endl;
    std::cout<<"Original X Size:"<<imageOrientation.x_orig_size<<std::endl;
    std::cout<<"Original Y Size:"<<imageOrientation.y_orig_size<<std::endl;
    std::cout<<"Pixel Aspect (H:V):"<<(unsigned int)imageOrientation.pixel_aspect[0]<<" : "<<(unsigned int)imageOrientation.pixel_aspect[1]<<std::endl;
    std::cout<<"Creation Time: "<<imageOrientation.creation_time<<std::endl;
    std::cout<<"Scanned Size X:"<<imageOrientation.x_scanned_size<<std::endl;
    std::cout<<"Scanned Size Y:"<<imageOrientation.y_scanned_size<<std::endl;
    printf("Input Device:%s\n",imageOrientation.input_dev);
    printf("Input Serial:%s\n",imageOrientation.input_serial);
    /*
    std::cout<<"Input Serial:"<<imageOrientation.input_serial<<std::endl;
    std::cout<<"Input Device:"<<imageOrientation.input_dev<<std::endl;*/


    std::cout<<"----------------------------------"<<std::endl;
    std::cout<<"\tMOTION PICTURE INFO:"<<std::endl;
    std::cout<<"Film Mgf. ID:"<<motionHeader.film_mfg_id[0]<<motionHeader.film_mfg_id[1]<<std::endl;
    std::cout<<"Film Type:"<<motionHeader.film_type[0]<<motionHeader.film_type[1]<<std::endl;
    std::cout<<"Prefix:";
    for (int i=0;i<6;i++)
        std::cout<<motionHeader.prefix[i];
    std::cout<<std::endl;
    std::cout<<"Count:";
    for (int i=0;i<4;i++)
        std::cout<<motionHeader.count[i];
    std::cout<<std::endl;
    std::cout<<"Format:"<<motionHeader.format<<std::endl;
    std::cout<<"Frame Position:"<<motionHeader.frame_position<<std::endl;
    std::cout<<"Sequence Lenght:"<<motionHeader.sequence_len<<std::endl;
    std::cout<<"Frame Rate:"<<motionHeader.frame_rate<<std::endl;
    std::cout<<"Slate Info:"<<motionHeader.slate_info<<std::endl;
    std::cout<<"Sutter Angle:"<<motionHeader.shutter_angle<<std::endl;
    std::cout<<"----------------------------------"<<std::endl;
    std::cout<<"\tTV INFO:"<<std::endl;
    std::cout<<"Time Code BCD:"<<tvHeader.tim_code<<std::endl;
    SMPTEBitsToString(tvHeader.tim_code,timeCodeString);
    std::cout<<"Time Code:"<< timeCodeString<<std::endl;
    std::cout<<"Frame Rate:"<<tvHeader.frame_rate<<std::endl;
    printf("\n***********************************\n\n");
}

gfcDPXMetaData getMetaData() {
    gfcDPXMetaData result;
    return result;
}

int DpxSlice::readHeader(const char *afileName) {
    //printf("*********\nREAD DPX HEADER\n*********\n");
    //printf("Opening file %s...",afileName);
    std::ifstream fs;
    fs.open(afileName,std::ios::binary);

    if (!fs) {
        printf("ERROR OPENING FILE!\n");
        printf("*********\n");
        return 0; //return false
    }
    //printf("done!\n");
    //printf("Reading file header...");
    fs.readsome((char*)&fileInfo,sizeof(dpx_file_information));

    fs.readsome((char*)&imageInfo,sizeof(dpx_image_information));

    fs.readsome((char*)&imageOrientation,sizeof(dpx_image_orientation));

    fs.readsome((char*)&motionHeader,sizeof(dpx_motion_picture_film_header));

    fs.readsome((char*)&tvHeader,sizeof(dpx_television_header));

    fs.readsome((char*)&userInfo,sizeof(dpx_UserInfo ));
    fs.close();
    // printf("done\n");
    swab=false;
    if (fileInfo.magic_num==0x58504453) { //if header is XPDS swab int, longs and floats
        swab=true;
        //printf("Swapping endians...");
        swabUInt32(&fileInfo.magic_num);
        swabUInt32(&fileInfo.offset);
        swabUInt32(&fileInfo.file_size);
        swabUInt32(&fileInfo.ditto_key);
        swabUInt32(&fileInfo.gen_hdr_size);
        swabUInt32(&fileInfo.ind_hdr_size);
        swabUInt32(&fileInfo.user_data_size);
        swabUInt32(&fileInfo.key);

        swabUInt16(&imageInfo.orientation);
        swabUInt16(&imageInfo.element_number);
        swabUInt32(&imageInfo.pixels_per_line);
        swabUInt32(&imageInfo.lines_per_image_ele);
        for (int i=0;i<8;i++) {
            swabUInt32(&imageInfo.image_element[i].data_sign);
            swabUInt32(&imageInfo.image_element[i].ref_low_data);
            swabFloat(&imageInfo.image_element[i].ref_low_quantity);
            swabUInt32(&imageInfo.image_element[i].ref_high_data);
            swabFloat(&imageInfo.image_element[i].ref_high_quantity);
            swabUInt16(&imageInfo.image_element[i].packing);
            swabUInt16(&imageInfo.image_element[i].encoding);
            swabUInt32(&imageInfo.image_element[i].data_offset);
            swabUInt32(&imageInfo.image_element[i].eol_padding);
            swabUInt32(&imageInfo.image_element[i].eo_image_padding);
        }

        swabUInt32(&imageOrientation.x_offset);
        swabUInt32(&imageOrientation.y_offset);
        swabFloat(&imageOrientation.x_center);
        swabFloat(&imageOrientation.y_center);
        swabUInt32(&imageOrientation.x_orig_size);
        swabUInt32(&imageOrientation.y_orig_size);
        swabUInt16(&imageOrientation.border[0]);
        swabUInt16(&imageOrientation.border[1]);
        swabUInt16(&imageOrientation.border[2]);
        swabUInt16(&imageOrientation.border[3]);
        swabUInt32(&imageOrientation.pixel_aspect[0]);
        swabUInt32(&imageOrientation.pixel_aspect[1]);

        swabUInt32(&motionHeader.frame_position);
        swabUInt32(&motionHeader.sequence_len);
        swabUInt32(&motionHeader.held_count);
        swabFloat(&motionHeader.frame_rate);
        swabFloat(&motionHeader.shutter_angle);

        swabUInt32(&tvHeader.tim_code);
        swabUInt32(&tvHeader.userBits);
        swabFloat(&tvHeader.hor_sample_rate);
        swabFloat(&tvHeader.ver_sample_rate);
        swabFloat(&tvHeader.frame_rate);
        swabFloat(&tvHeader.time_offset);
        swabFloat(&tvHeader.gamma);
        swabFloat(&tvHeader.black_level);
        swabFloat(&tvHeader.black_gain);
        swabFloat(&tvHeader.break_point);
        swabFloat(&tvHeader.white_level);
        swabFloat(&tvHeader.integration_times);

        // printf("done\n");
    }
    sprintf(fileName,"%s",afileName);
    SMPTEBitsToString(tvHeader.tim_code,timeCodeString);
    // printf("*********\n");
    return 1;
}



unsigned int  DpxSlice::samplesPerPixel() {
    unsigned int
    samples_per_pixel=0;

    switch (imageInfo.image_element[0].descriptor) {
    case ImageElementUnspecified:
    case ImageElementRed:
    case ImageElementGreen:
    case ImageElementBlue:
    case ImageElementAlpha:
    case ImageElementLuma:
    case ImageElementColorDifferenceCbCr:
        samples_per_pixel=1;
        break;
    case ImageElementRGB:
        samples_per_pixel=3;
        break;
    case ImageElementRGBA:
    case ImageElementABGR:
        samples_per_pixel=4;
        break;
    case ImageElementCbYCrY422:
        /* CbY | CrY | CbY | CrY ..., even number of columns required. */
        samples_per_pixel=2;
        break;
    case ImageElementCbYACrYA4224:
        /* CbYA | CrYA | CbYA | CrYA ..., even number of columns required. */
        samples_per_pixel=3;
        break;
    case ImageElementCbYCr444:
        samples_per_pixel=3;
        break;
    case ImageElementCbYCrA4444:
        samples_per_pixel=4;
        break;
    default:
        samples_per_pixel=0;
        break;
    }

    return samples_per_pixel;
}

int DpxSlice::readSlice(long startOffset, long numLines) { //the offset is from the begining of the data, not the begining of the file
    printf("\n*********\nREAD DPX SLICE\n*********\n");
    unsigned int *samples, //parsed sample array
    *samples_itr; //current sample
    unsigned char *scanline; //raw info read from the file
    unsigned int bytesPerRow=DPXRowBytes(1,imageInfo.pixels_per_line*samplesPerPixel(),(int)imageInfo.image_element[0].bit_size,imageInfo.image_element[0].packing),
                             readBytes,
                             samples_per_row;
    //bytesPerRow=bytesPerRow;
    unsigned int endian_type = swab ? LSBEndian : MSBEndian;
    bool swap_word_datums=false;

    /*
          Are datums returned in reverse order when extracted from a
          32-bit word?  This is to support Note 2 in Table 1 which
          describes how RGB/RGBA are returned in reversed order for the
          10-bit "filled" format.  Note 3 refers to Note 2 so presumably
          the same applies for ABGR.  The majority of YCbCr 4:2:2 files
          received have been swapped (but not YCbCr 4:4:4 for some
          reason) so swap the samples for YCbCr 4:2:2 as well.
        */
    if ((imageInfo.image_element[0].descriptor == ImageElementRGB) ||
            (imageInfo.image_element[0].descriptor == ImageElementRGBA) ||
            (imageInfo.image_element[0].descriptor == ImageElementABGR) ||
            (imageInfo.image_element[0].descriptor == ImageElementCbYCrY422) ||
            (imageInfo.image_element[0].descriptor == ImageElementCbYACrYA4224) ||
            (imageInfo.image_element[0].descriptor == ImageElementLuma)) {
        if (((int)imageInfo.image_element[0].bit_size == 10) && (imageInfo.image_element[0].packing != PackingMethodPacked))
            swap_word_datums = true;
    }

    //allocate space for a sample line
    samples_per_row=samplesPerPixel()*imageInfo.pixels_per_line;
    samples=new unsigned int[bytesPerRow];
    assert(samples);
    //allocate space for a scanline
    scanline = new unsigned char [bytesPerRow];
    assert(scanline);


    //open the file and scan to the startOffset
    printf("Opening file %s...",fileName);
    std::ifstream fs(fileName, std::ios::binary);
    if (!fs) {
        printf("ERROR READING FILE %s, could not open file;\n",fileName);
        printf("*********\n");
        return 0;
    }
    fs.seekg(startOffset);
    //printf("done\n");
    printf("Allocating space to store pixels...");
    long totalPixels=imageInfo.pixels_per_line*imageInfo.lines_per_image_ele;
    switch (imageInfo.image_element[0].descriptor) {
    case ImageElementRGB:
        printf("\tRGB Image: %i pixels needed...\n",totalPixels);
        rgb8Pixels=new pixelRGB8Bit[totalPixels];
        if (!rgb8Pixels) {
            printf("ERROR ALLOCATING SPACE\n");
        }
        break;
    case ImageElementRGBA:
        printf("\tRGB Image: %i pixels needed...\n",totalPixels);
        rgb16Pixels=new pixelRGB16Bit[totalPixels];
        if (!rgb16Pixels) {
            printf("ERROR ALLOCATING SPACE\n");
        }
        break;

    default:
        printf("Image Descriptor not supported: %i\n");
        return 1;
        break;
    }
    //for each in numlines
    printf("Reading %i scanlines of %i bytes each\n",numLines,bytesPerRow);
    unsigned int maxValue=0;
    unsigned int minValue=99999999;
    printf("2porBitsize:%f\n",pow((float)2,(int)imageInfo.image_element[0].bit_size));
    float quantum=255.0/pow((float)2,(int)imageInfo.image_element[0].bit_size);
    for (int i=0;i<numLines;i++) {
        //read one scanline
        void  *scanline_data=scanline;
        readBytes=fs.readsome((char*)scanline_data,bytesPerRow);
        if (readBytes!=bytesPerRow) {
            printf("ERROR: Could not read the whole scanline (scanline %i)!\n (%i bytes read, should have read %i)\n",i,readBytes,bytesPerRow);
            printf("*********\n");
            //break;//return 0;
        }

        //printf("Read scanline %i (%i bytes)\n",i,readBytes);
        //convert scanline into samples according to packing and endianess

        ReadRowSamples(scanline,samples_per_row, (int)imageInfo.image_element[0].bit_size,imageInfo.image_element[0].packing,endian_type,swap_word_datums,samples);
        
        
        //asign sample to pixel structure
        samples_itr=samples;
        int pixelIndex=pixelIndex=i*imageInfo.pixels_per_line+imageInfo.pixels_per_line;
        int prePixelIndex=pixelIndex=i*imageInfo.pixels_per_line+imageInfo.pixels_per_line;
        switch (imageInfo.image_element[0].descriptor) {

        case ImageElementRGB:
            /* RGB order */

            for (int x=imageInfo.pixels_per_line; x > 0; x--) {
                pixelIndex=prePixelIndex-x;
                rgb8Pixels[pixelIndex].r=(*samples_itr++)>>2;
                rgb8Pixels[pixelIndex].g=(*samples_itr++)>>2;
                rgb8Pixels[pixelIndex].b=(*samples_itr++)>>2;//*/

                /*rgb8Pixels[pixelIndex].r=*samples_itr++*quantum;
                rgb8Pixels[pixelIndex].g=*samples_itr++*quantum;
                rgb8Pixels[pixelIndex].b=*samples_itr++*quantum;//*/

                /* rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].r=*samples_itr++;
                             rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].g=*samples_itr++;
                             rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].b=*samples_itr++;//*/

                /* rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].r=*samples_itr++*quantum;
                 rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].g=*samples_itr++*quantum;
                 rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].b=*samples_itr++*quantum;//*/

                /*printf("Line:%i column:%i\n",i,x);
                            printf("R:%i\n",*samples_itr++);
                            printf("G:%i\n",*samples_itr++);
                            printf("B:%i\n\n",*samples_itr++);*/
                /* rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].r=testLut.getOutput(*samples_itr++);
                             rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].g=testLut.getOutput(*samples_itr++);
                             rgb8Pixels[i*imageInfo.pixels_per_line+imageInfo.pixels_per_line-x].b=testLut.getOutput(*samples_itr++);*/
                // maxValue=(rgb8Pixels[i*imageInfo.pixels_per_line+x].r>maxValue)?rgb8Pixels[i*imageInfo.pixels_per_line+x].r:maxValue;
                // minValue=(rgb8Pixels[i*imageInfo.pixels_per_line+x].r<minValue)?rgb8Pixels[i*imageInfo.pixels_per_line+x].r:minValue;
            }
            break;
            /*case ImageElementRGBA:
                // RGB order
                for (x=image->columns; x > 0; x--)
                {
                    SetRedSample(q,*samples_itr++);
                    SetGreenSample(q,*samples_itr++);
                    SetBlueSample(q,*samples_itr++);
                    SetOpacitySample(q,*samples_itr++);
                    q++;
                }
                break;
            case ImageElementABGR:
                // ARGB order
                for (x=image->columns; x > 0; x--)
                {
                    SetOpacitySample(q,*samples_itr++);
                    SetRedSample(q,*samples_itr++);
                    SetGreenSample(q,*samples_itr++);
                    SetBlueSample(q,*samples_itr++);
                    q++;
                }
                break;*/

        default:
            break;
        }
        //lut down to 8 bits.
    }
    /*printf("MaxValue Read:%i\n",maxValue);
    printf("MinValue Read:%i\n",minValue);
    printf("Closing File...");*/
    fs.close();
    //printf("done\n");
    printf("*********\n");
    return 1;
}



int DpxSlice::releasePixelMemory() {
    delete [] rgb8Pixels;
    return 1;
}
