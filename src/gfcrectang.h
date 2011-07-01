#ifndef GFCRECTANG_H
#define GFCRECTANG_H

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

class gfcRectang;

class gfcRectang{
public:
   
    ~gfcRectang();
    gfcRectang(){x=y=w=h=-1;};
	gfcRectang(int px, int py, int pw, int ph):x(px),y(py),w(pw),h(ph){};
	
	gfcRectang intersection(const gfcRectang &b);
	bool intersects(const gfcRectang &b);

		void set(int X, int Y, int W, int H);
		
		int x;
		int y;
		int w;
		int h;
	
};

class gfcRectangf{
public:
   
    ~gfcRectangf();
    gfcRectangf(){x=y=w=h=-1.0;};
	gfcRectangf(float px, float py, float pw, float ph):x(px),y(py),w(pw),h(ph){};
		
		
		void set(float X, float Y, float W, float H);
		
		float x;
		float y;
		float w;
		float h;
};



#endif
