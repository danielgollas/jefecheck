#ifndef __DPX_IMAGE_INFORMATION_INTERFACE_H
#define __DPX_IMAGE_INFORMATION_INTERFACE_H

class dpx_image_informationInterface
{
public:
    dpx_image_informationInterface() {}
    virtual ~dpx_image_informationInterface() {}


private:
    dpx_image_informationInterface( const dpx_image_informationInterface& source );
    void operator = ( const dpx_image_informationInterface& source );
};


#endif // __DPX_IMAGE_INFORMATION_INTERFACE_H
