#extension GL_ARB_texture_rectangle : require
uniform sampler2DRect Source;
uniform float Red;
uniform float Cyan;

uniform vec2 texCoord0;

void main()
{	
		float middle=texCoord0.x/2.0;
		float quarter=middle/2.0;
		float texCoordS=gl_TexCoord[0].s;
				
		if(texCoordS > middle+quarter || texCoordS < quarter){
			//outside of the middle
			gl_FragColor = vec4(0.0,0.0,0.0,0.0);
		}
		else{
			//in the middle section
		vec4 left=texture2DRect(Source,gl_TexCoord[0].st-vec2(quarter,0));		
		vec4 right=texture2DRect(Source,gl_TexCoord[0].st+vec2(quarter,0));
				
		right=(right+vec4(Red,0,0,0));
		left=(left+vec4(0,Cyan,Cyan,0));
		gl_FragColor =min(left,right);
			
		}
		
		/*vec4 left=texture2DRect(Left,gl_TexCoord[0].st);		
		vec4 right=texture2DRect(Right,gl_TexCoord[1].st);
				
		right=(right+vec4(Red,0,0,0));
		left=(left+vec4(0,Cyan,Cyan,0));
		gl_FragColor =min(left,right);*/
		
		
	
}
