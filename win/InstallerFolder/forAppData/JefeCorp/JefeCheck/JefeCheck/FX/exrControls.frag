uniform sampler2DRect image;
uniform float Exposure;
uniform float Gamma;

void main()
{	
		vec4 texel=texture2DRect(image,gl_TexCoord[0].st);
		texel=texel*pow(vec4(2.0),vec4((Exposure))); //adjust exposure
		gl_FragColor =pow(texel,vec4((1.0/Gamma))); //apply video gammma
}