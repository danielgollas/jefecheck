#ifndef GFCNOTETEXT_H
#define GFCNOTETEXT_H

#include "gfcnote.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcNoteText : public gfcNote
{
public:
    gfcNoteText();

    ~gfcNoteText();

     virtual void draw();
     virtual void fillSpecificPane();
};

#endif
