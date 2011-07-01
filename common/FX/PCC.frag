uniform float BrightnessR,BrightnessG,BrightnessB;
uniform float SaturationR,SaturationG,SaturationB;
uniform float ContrastR,ContrastG,ContrastB;
uniform float AvgLuminance;
uniform sampler2DRect image;
const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);

void main()
{	
		
		vec4 tmp=texture2DRect(image,gl_TexCoord[0].st )*vec4(BrightnessR,BrightnessG,BrightnessB,1.0);
		vec4 tmpIntensity=vec4(dot(tmp.rgb,lumCoeff));
		vec4 satResult= vec4(mix(tmp.r,tmpIntensity.r,1.0-SaturationR),mix(tmp.g,tmpIntensity.g,1.0-SaturationG),mix(tmp.b,tmpIntensity.b,1.0-SaturationB),1.0);
		vec3 avgLuminanceVec=vec3(AvgLuminance);
		vec4 contResult=vec4(mix(avgLuminanceVec.r,satResult.r,ContrastR),mix(avgLuminanceVec.g,satResult.g,ContrastG),mix(avgLuminanceVec.b,satResult.b,ContrastB),tmp.a);
		gl_FragColor = contResult;
}