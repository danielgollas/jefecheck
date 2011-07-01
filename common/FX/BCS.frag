#extension GL_ARB_texture_rectangle : require
uniform float Brightness;
uniform float Saturation;
uniform float Contrast;
uniform float AvgLuminance;
uniform sampler2DRect image;
const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);

void main()
	{	
		vec4 tmp=texture2DRect(image,gl_TexCoord[0].st );
		float originalAlpha=tmp.a;
		tmp*=Brightness;
		vec4 tmpIntensity=vec4(dot(tmp.rgb,lumCoeff));
		vec4 satResult= mix(tmp,tmpIntensity,1.0-Saturation);
		vec3 avgLuminanceVec=vec3(AvgLuminance);
		vec3 contResult=mix(avgLuminanceVec,satResult.rgb,Contrast);
		gl_FragColor = vec4(contResult,originalAlpha);
}