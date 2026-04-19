uniform sampler2DRect image;

void main()
{		
		
		vec4 c11,c12,c13;
		vec4 c21,c22,c23;
		vec4 c31,c32,c33;
		vec4 c=gl_TexCoord[0];

		//get all the neighbouring pixels
			
		float kernel[9];//={0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625};
		kernel[0]=0.0625;
		kernel[1]=0.125;
		kernel[2]=0.0625;
		kernel[3]=0.125;
		kernel[4]=0.25;
		kernel[5]=0.0125;
		kernel[6]=0.0625;
		kernel[7]=0.125;
		kernel[8]=0.025;
		
		c11=texture2DRect(image,vec2(c.s-1.0,c.t-1.0))*kernel[0];
		c12=texture2DRect(image,vec2(c.s-1.0,c.t))*kernel[1];
		c13=texture2DRect(image,vec2(c.s-1.0,c.t+1.0))*kernel[2];

		c21=texture2DRect(image,vec2(c.s,c.t-1.0))*kernel[3];
		c22=texture2DRect(image,vec2(c.s,c.t))*kernel[4];
		c23=texture2DRect(image,vec2(c.s,c.t+1.0))*kernel[5];

		c31=texture2DRect(image,vec2(c.s+1.0,c.t-1.0))*kernel[6];
		c32=texture2DRect(image,vec2(c.s+1.0,c.t))*kernel[7];
		c33=texture2DRect(image,vec2(c.s+1.0,c.t+1.0))*kernel[8];

		gl_FragColor =c11+c12+c13+c21+c22+c23+c31+c32+c33;

}