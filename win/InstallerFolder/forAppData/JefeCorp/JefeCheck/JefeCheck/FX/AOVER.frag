uniform sampler2DRect Top;
uniform sampler2DRect Bottom;
uniform float Premultiplied;
void main()
{	
	
		vec4 top=texture2DRect(Top,gl_TexCoord[1].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
				
		if(Premultiplied==1.0)
		gl_FragColor =top+bottom*(1.0-top.a);
		else
		gl_FragColor =mix(top,bottom,(1.0-top.a));
	
}