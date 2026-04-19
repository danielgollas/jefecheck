uniform sampler2DRect Top;
uniform sampler2DRect Bottom;
uniform float Absolut;

void main()
{	
		vec4 top=texture2DRect(Top,gl_TexCoord[1].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		
		if(Absolut==1.0)
		gl_FragColor =abs(vec4(bottom-top));
		else
		gl_FragColor =bottom-top;

	
}