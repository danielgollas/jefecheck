uniform sampler2DRect Top;
uniform sampler2DRect Bottom;
uniform sampler2DRect Matte;
uniform float Method;

void main()
{	
	if(Method==3.0) //alpha==3
	{
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
		gl_FragColor =mix(top,bottom,(1.0-matte.a));
	}
	else if(Method==0.0){ //R==0
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
		gl_FragColor =mix(top,bottom,(1.0-matte.r));
	}
	else if(Method==1.0){ //G==1
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
		gl_FragColor =mix(top,bottom,(1.0-matte.g));
	}
	else if(Method==2.0){ //B==2
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
		gl_FragColor =mix(top,bottom,(1.0-matte.b));
	}
	else if(Method==3.0){ //Y==3
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);
		//get luminance
		const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);
		float luminance=dot(matte.rgb,lumCoeff);
		gl_FragColor =mix(top,bottom,(1.0-luminance));
	}
	else if(Method==4.0){ //V==4
		vec4 top=texture2DRect(Top,gl_TexCoord[2].st);		
		vec4 bottom=texture2DRect(Bottom,gl_TexCoord[0].st);
		vec4 matte=texture2DRect(Matte,gl_TexCoord[1].st);		
		gl_FragColor =mix(top,bottom,(1.0-matte.b));
	}
	else{
		gl_FragColor =vec4(0,1,0,0);
	}
}