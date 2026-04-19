uniform sampler2DRect image;
uniform float ondita;

void main()
{	
		
		gl_FragColor = texture2DRect(image,gl_TexCoord[0].st+ondita*sin(gl_TexCoord[0].st));
}