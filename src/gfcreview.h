#ifndef GFCREVIEW_H
#define GFCREVIEW_H

#include "gfcrevision.h"
#include <vector>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcReview{
public:
    gfcReview();

    ~gfcReview();
    std::vector<gfcRevision> revisions;
};

#endif
