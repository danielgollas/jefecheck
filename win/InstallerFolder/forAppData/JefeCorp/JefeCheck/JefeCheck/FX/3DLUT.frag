#extension GL_ARB_texture_rectangle : require
uniform sampler2DRect image;
uniform sampler3D LUT;
//uniform float lutSize;
uniform float LUT_size;

void main()
{	
		vec4 cubeCoords=texture2DRect(image,gl_TexCoord[0].st)*((LUT_size-1.0)/LUT_size) + 1.0 /(LUT_size*2.0);		
		gl_FragColor = vec4(vec3(texture3D(LUT,cubeCoords.rgb).rgb),cubeCoords.a);
}