uniform sampler2DRect image;
uniform float amount;
void main()
{		
		
		vec4 result;
		vec4 c=gl_TexCoord[0];
		float i, j, count;
		count=floor(amount/2.0);
		float mult=1.0/((count*2.0)*(count*2.0));
		for(i=-count; i<count;i++)
		{	
			for(j=-count; j<count;j++)
			{
				result+=texture2DRect(image,vec2(c.s+i,c.t+j))*mult;				
			}
		}
				
		gl_FragColor=result;						

}