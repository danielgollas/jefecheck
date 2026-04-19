uniform sampler2DRect first;
uniform sampler2DRect second;

uniform float start;
uniform float duration;
uniform float currentFrame;

void main()
{
	float end=start+duration;
	float amount=clamp((currentFrame-start)/(end-start),0.0,1.0);
	gl_FragColor = mix( texture2DRect(first,gl_TexCoord[0].st), texture2DRect(second,gl_TexCoord[1].st), amount );
}