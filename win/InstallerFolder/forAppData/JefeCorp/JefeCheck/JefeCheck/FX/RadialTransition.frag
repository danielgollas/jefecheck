uniform sampler2DRect first;
uniform sampler2DRect second;

uniform vec2 texCoord0;
uniform vec2 texCoord1;

uniform float start;
uniform float duration;
uniform float currentFrame;
uniform float smoothness;
void main()
{
	float end=start+duration;
	float amount=clamp((currentFrame-start)/(end-start),0.0,1.0);
	vec2 center=vec2(texCoord0/2.0);
	float dist=distance(gl_TexCoord[0].st,center);
	dist=dist/max(texCoord0.s,texCoord0.t); //normalize distance, max distance can be distance to the corner.
	dist=smoothstep(amount-smoothness,amount,dist);	//smooth with smoothness parameter
	gl_FragColor = mix( texture2DRect(first,gl_TexCoord[0].st), texture2DRect(second,gl_TexCoord[1].st), dist );
}