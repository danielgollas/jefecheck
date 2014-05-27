uniform sampler2DRect Left;
uniform sampler2DRect Right;
uniform vec2 texCoord0;
uniform vec2 texCoord1;
void main()
{	
        float middle=texCoord0.x/2.0;
		float texCoordS=gl_TexCoord[0].s;
        vec4 theResult;
		if(texCoordS > middle ){
			//use right side
			theResult=texture2DRect(Right,(gl_TexCoord[1].st-vec2(middle,0.0))*vec2(2.0,1.0));
		}
		else{
			//use left side
            theResult=texture2DRect(Left,gl_TexCoord[0].st*vec2(2.0,1));
		}
        
		gl_FragColor =theResult;
}