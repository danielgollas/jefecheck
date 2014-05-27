uniform sampler2DRect image;
uniform float startEven;
uniform float currentFrame;
uniform float intensity;
void main()
{	
		
		vec4 color=texture2DRect(image,gl_TexCoord[0].st);
		float showOrNot=step(1.0, mod(gl_TexCoord[0].t+mod(currentFrame,2.0),2.0));
		gl_FragColor= color*(vec4(clamp(showOrNot+intensity,0.0,1.0)));
}