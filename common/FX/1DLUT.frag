#extension GL_ARB_texture_rectangle : require
uniform sampler2DRect image;
uniform sampler1D LUT;

void main()
{	
		vec4 cubeCoords=texture2DRect(image,gl_TexCoord[0].st);		
		vec4 outColor=vec4(texture1D(LUT,cubeCoords.r).r,texture1D(LUT,cubeCoords.g).g,texture1D(LUT,cubeCoords.b).b,cubeCoords.a);
		//gl_FragColor = vec4(vec3(texture1D(LUT,cubeCoords.rgb*4.0).rgb),1);
		gl_FragColor = outColor;
}