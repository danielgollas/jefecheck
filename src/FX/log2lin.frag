#extension GL_ARB_texture_rectangle : require
uniform float refWhite;
uniform float refBlack;
uniform float dispGamma;
uniform float softClip;
uniform sampler2DRect image;

float breakPoint;
float gain;
float offset;
float kneesoft;
float kneegain;
float first;
float second;
float doIt(in float intensity) {
	
		
		//clamp to refBlack
		if (intensity < refBlack){
		return(0.0);
		}
		
//compute between refBlack and Breakpoint
//OUT = 10 ^ ((IN-Refwhite) * 0.002/0.6) ^ (Dispgamma/1.7) * Gain - Offset

		if(intensity<breakPoint)
		{
			float result=pow( pow(10.0,(intensity-refWhite) *0.002/0.6), dispGamma/1.7  )	*gain	-offset;
			return(result/255.0);
			//return( (gain/255/2.0));
			
		}
		
		//the last default case is where 
		//5.4 Compute a Softclip above Breakpoint
		//OUT = (IN - Breakpoint) ^ (Softclip/100) * Kneegain + Kneeoffset
		
		float result = pow(intensity-breakPoint, softClip/100.0)*kneegain+kneesoft;
		return(min(max(result,0.0),255.0)/255.0);
		
		//return intensity/255.0;
	}

void main()
	{	
		vec4 tmp=texture2DRect(image,gl_TexCoord[0].st )*1024.0;
		
			

		breakPoint=refWhite-softClip;

//Gain = 225 / (1-10 ^ (Refblack-Refwhite) * 0.002/0.6) ^ (Dispgamma/1.7))
		
		first=1.0-pow(10.0,(refBlack-refWhite))*(0.002/0.6);
		second=pow (first, dispGamma/1.7 );
		
		//225.0 / 	
		gain = 255.0/second;
		offset=gain-255.0;
		
		kneesoft=pow (pow(10.0, (breakPoint-refWhite)*0.002/0.6) , dispGamma/1.7) *gain - offset;
		kneegain = (255.0 -kneesoft) / pow(5.0*softClip ,softClip/100.0 );
		//float result=second/2.0;
		//gl_FragColor=vec4(result,result,result,1.0);
				
		gl_FragColor = vec4(doIt(tmp.r),doIt(tmp.g),doIt(tmp.b),tmp.a);
}