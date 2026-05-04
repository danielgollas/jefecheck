//Callback definition for UI elements

#ifndef UICALLBACKS_H
#define UICALLBACKS_H

#include <glad/glad.h>
#include <FL/Fl.H>
#include <FL/Fl_Menu.H>
#include "UIConstants.h"
#include <string>
#define GFC_NUM_OF_SEQUENCES 4

#include "gfcrenderparams.h"


void controlBarCB(Fl_Widget* o , void* v);
void tracksBarCB(Fl_Widget* o , void* v);
void mouseCB(Fl_Widget* o , void* v);
void menuCB(Fl_Menu_* o , void* v);
void loadCB(Fl_Widget* o , void* v);
void lutCB(Fl_Widget* o , void* v);
void fxCB(Fl_Widget* o , void* v);
void RenderCB(Fl_Widget* o,void* v);
void gammaCB(Fl_Widget* o , void* v);
void remoteCB(Fl_Widget* o, void *v);
void reviewToolsCB(Fl_Widget* o, void *v);
void PreferencesCB(Fl_Widget* o , void* v);
void moreOptionsCB(Fl_Widget* o , void* v);
void exitRoutine();
void Render(gfcRenderParams params);
void rebuildFXHashMap();
void rebuildLUTHashMap();



#endif
