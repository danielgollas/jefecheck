uniform float Multiplier;
uniform sampler2DRect first;
uniform sampler2DRect second;

void main()
{	
		gl_FragColor = abs(texture2DRect(first,gl_TexCoord[0].st)-texture2DRect(second,gl_TexCoord[1].st))*Multiplier;
}