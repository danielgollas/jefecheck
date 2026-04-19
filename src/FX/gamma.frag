uniform sampler2DRect image;
uniform float Gamma;

void main()
{	
		vec4 texel=texture2DRect(image,gl_TexCoord[0].st);
		gl_FragColor = pow(texel,vec4((1.0/Gamma)));
}