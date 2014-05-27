uniform sampler2DRect image;

void main()
{		
		
		vec4 c11,c12,c13;
		vec4 c21,c22,c23;
		vec4 c31,c32,c33;
		vec4 c=gl_TexCoord[0];
		//get all the neighbouring pixels
		c11=texture2DRect(image,vec2(c.s-1.0,c.t-1.0));
		c12=texture2DRect(image,vec2(c.s-1.0,c.t));
		c13=texture2DRect(image,vec2(c.s-1.0,c.t+1.0));

		c21=texture2DRect(image,vec2(c.s,c.t-1.0));
		c22=texture2DRect(image,vec2(c.s,c.t));
		c23=texture2DRect(image,vec2(c.s,c.t+1.0));

		c31=texture2DRect(image,vec2(c.s+1.0,c.t-1.0));
		c32=texture2DRect(image,vec2(c.s+1.0,c.t));
		c33=texture2DRect(image,vec2(c.s+1.0,c.t+1.0));

		/*
		float kernel[9]={0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625};
		c11=texture2DRect(image,vec2(c.s-1,c.t-1))*kernel[0];
		c12=texture2DRect(image,vec2(c.s-1,c.t))*kernel[1];
		c13=texture2DRect(image,vec2(c.s-1,c.t+1))*kernel[2];

		c21=texture2DRect(image,vec2(c.s,c.t-1))*kernel[3];
		c22=texture2DRect(image,vec2(c.s,c.t))*kernel[4];
		c23=texture2DRect(image,vec2(c.s,c.t+1))*kernel[5];

		c31=texture2DRect(image,vec2(c.s+1,c.t-1))*kernel[6];
		c32=texture2DRect(image,vec2(c.s+1,c.t))*kernel[7];
		c33=texture2DRect(image,vec2(c.s+1,c.t+1))*kernel[8];*/

		gl_FragColor =(c11+c12+c13+c21+c22+c23+c31+c32+c33)/9.0;						
		//gl_FragColor = vec4(1.0,0,0,0);//c22;
		//gl_FragColor = c21;


}