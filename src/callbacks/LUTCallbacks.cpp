// LUT-management callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"

void lutCBFillLoadedScroll() {

}

void lutCB ( Fl_Widget* o , void* v ) {

	switch ( ( long ) v ) {
	case LUTDONE_ID:

		break;

	case LUTREFRESH_ID:

		break;

	case LUTBROWSE_ID: {



					   }
					   break;


	case LUTAUTOLOAD_ID: {

						 }
						 break;

	case LUTDELETE_ID:

		{

		}



		break;

	default:
		printf ( "Unmanaged lutCB callback, ID: %i\n",long ( v ) );
		break;
	}
}
