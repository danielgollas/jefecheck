uniform float Amount;
uniform sampler2DRect first;
uniform sampler2DRect second;
uniform float X;
uniform float Y;
uniform float Slope;
uniform float Smoothness;
uniform float lineSize;
uniform float lineOpacity;
void main()
{	
		//float lineSize=5.0;
		/*if((gl_TexCoord[0].s>Slope*(gl_TexCoord[0].t-X)+Y))
			gl_FragColor=texture2DRect(first,gl_TexCoord[0].st);
		else
			gl_FragColor=texture2DRect(second,gl_TexCoord[1].st);*/

		float x=gl_TexCoord[0].s;
		float y=gl_TexCoord[0].t;
		vec4 lineColor=vec4(0.0,0.0,0.0,0);

		/*if(abs(y-Y)<lineSize*2 && abs(x-X)<lineSize)
		{
			lineColor=vec4(1.0,1.0,1.0,0);
		}*/
		
		float eY=y-Y;
		float eX=x-X;
		if( abs(eY-(eX/Slope))<lineSize || abs(eY*Slope-(eX))<lineSize)
		{
			lineColor=vec4(lineOpacity,lineOpacity,lineOpacity,0.0);
		}

		gl_FragColor=lineColor+mix(texture2DRect(first,gl_TexCoord[0].st),    texture2DRect(second,gl_TexCoord[1].st),           smoothstep(  0.0, 1.0,   ( ((gl_TexCoord[0].s-Slope*(gl_TexCoord[0].t-Y) -X )) / Smoothness/max(abs(Slope),1.0)  )       ));
		

		//float dist=gl_TexCoord[0].x
		
//gl_FragColor = mix(texture2DRect(first,gl_TexCoord[0].st),texture2DRect(second,gl_TexCoord[1].st),Amount);
}