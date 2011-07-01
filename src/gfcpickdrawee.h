#ifndef GFCPICKINGDRAWEE_H
#define GFCPICKINGDRAWEE_H

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPickDrawee{
public:
    gfcPickDrawee();

    ~gfcPickDrawee();
    
    virtual void drawForPicking()=0; //draws in a certain way for picking, the plate manager should be one of those drawees.
    
private:
    

};

#endif
