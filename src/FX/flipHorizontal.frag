uniform vec2 texCoord0;
uniform sampler2DRect theImage;
uniform float width;
uniform float height;
void main()
{	
		gl_FragColor=texture2DRect(theImage,gl_TexCoord[0].st);
		
}