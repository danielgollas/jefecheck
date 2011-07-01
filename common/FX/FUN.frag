uniform sampler2DRect texture;
uniform float centerX;
uniform float centerY;
uniform float radius;
uniform float zoom;
uniform float curve;
uniform vec2 texCoord0;
uniform float effect;
void main()
{	
		//float resultR, resultG, resultB;
		vec2 center =vec2(centerX,centerY);
		vec2 texCoord = (gl_TexCoord[0].xy)/texCoord0.xy; // [0.0 ,1.0] x [0.0, 1.0]
				
		vec2 normCoord = 2.0 * texCoord - 1.0; // [-1.0 ,1.0] x [-1.0, 1.0]

		//go to polar coordinates
		float r = length(normCoord); // to polar coords		
		float phi = atan(normCoord.y, normCoord.x)+centerY; // to polar coords
		
		if(effect==0.0){
			r = pow(r, 1.0/curve) * zoom;	// squeeze
			normCoord.x = r * cos(phi);
			normCoord.y = r * sin(phi);
		}
		else if(effect==1.0){
			r = r * smoothstep(-0.1, curve, r); // bulge 
			normCoord.x = r * cos(phi);
			normCoord.y = r * sin(phi);
		}
		else if(effect==2.0){
			r = curve * r - r * smoothstep(0.0, zoom, r); //dent
			normCoord.x = r * cos(phi);
			normCoord.y = r * sin(phi);
		}
		else if(effect==3.0){
			phi = phi + (1.0 - smoothstep(-curve, curve, r)) * zoom; // twirl 
			normCoord.x = r * cos(phi);
			normCoord.y = r * sin(phi);
		}
		
		else if(effect==4.0){
			 if (r > zoom) r = zoom; // light tunnel
			 normCoord.x = r * cos(phi);
			normCoord.y = r * sin(phi);

		}
		else if(effect==5.0){
  			//stretch
			vec2 s = sign(normCoord);
  			normCoord = abs(normCoord);
  			normCoord = 0.5 * normCoord + 0.5 * smoothstep(zoom/2.0, zoom, normCoord) * normCoord;
 			normCoord = s * normCoord;
		}
			else if(effect==6.0){
  			//mirror
			normCoord.x = normCoord.x * sign(normCoord.x); // mirror 
		}

				
		
		
		texCoord = normCoord / 2.0 + 0.5; // [0.0 ,1.0] x [0.0, 1.0]
		
		//return from the normalized coordinates to the glRect coordinates
		texCoord=texCoord*texCoord0+center;
		
		gl_FragColor = texture2DRect(texture, texCoord);

		//resultR=texCoord.x;
		//resultG=texCoord.y;
		//gl_FragColor = vec4(resultR,resultG,resultB,0);
		
		
}