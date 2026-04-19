uniform sampler2DRect Left;
uniform sampler2DRect Right;
uniform float Red;
uniform float Cyan;
void main()
{	
	
		vec4 left=texture2DRect(Left,gl_TexCoord[0].st);		
		vec4 right=texture2DRect(Right,gl_TexCoord[1].st);
				
		right=(right+vec4(Red,0,0,0));
		left=(left+vec4(0,Cyan,Cyan,0));
		gl_FragColor =min(left,right);
		
		
	
}
