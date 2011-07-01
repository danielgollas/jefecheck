#ifndef GFCREVISION_H
#define GFCREVISION_H

#include <vector>
#include <string>
#include "gfcnotetext.h"
#include <time.h>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcRevision{
public:
    gfcRevision();

    ~gfcRevision(); 

    std::vector<gfcNote*> notes;
    std::string author;
    time_t created;
    time_t modified;
    bool locked;
};

#endif
