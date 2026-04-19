uniform sampler2DRect Image1;
uniform sampler2DRect Image2;
uniform float factor1;
uniform float factor2;
void main()
{	
		vec4 first=texture2DRect(Image1,gl_TexCoord[0].st);		
		vec4 second=texture2DRect(Image2,gl_TexCoord[1].st);
		
		gl_FragColor =first*factor1+second*factor2;

	
}