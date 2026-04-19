uniform sampler2DRect Source;
uniform sampler2DRect Matte;
uniform float Method;
uniform float Outside;
void main()
{	
	vec4 source=texture2DRect(Source,gl_TexCoord[0].st);		
	vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
	if(Method==3.0) //alpha==3
	{
		
		gl_FragColor =vec4(mix(vec4(0),source,abs(Outside-matte.a)));
	}
	else if(Method==0.0){ //R==0
		
		gl_FragColor =vec4(mix(vec4(0),source,abs(Outside-matte.r)));
	}
	else if(Method==1.0){ //G==1
		
		gl_FragColor =vec4(mix(vec4(0),source,abs(Outside-matte.g)));
	}
	else if(Method==2.0){ //B==2
		
		gl_FragColor =vec4(mix(vec4(0),source,abs(Outside-matte.b)));
	}
	else if(Method==4.0){ //Y==4
		//get luminance
		const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);
		float luminance=dot(matte.rgb,lumCoeff);
		gl_FragColor =vec4(mix(vec4(0),source,abs(Outside-luminance)));
	}
}