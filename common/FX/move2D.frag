uniform vec2 texCoord0;
uniform sampler2DRect theImage;


uniform float moveX;
uniform float moveY;


void main()
{	
		vec2 move=vec2(moveX,moveY);
		vec2 realCoord=gl_TexCoord[0].st+move;
				
		if( realCoord.x < texCoord0.x && realCoord.x>0.0 && realCoord.y < texCoord0.y && realCoord.y>0.0)
		{	
			gl_FragColor = texture2DRect(theImage,realCoord)+vec4(0.0,0.0,0.0,0.0);
		}
		else{
			gl_FragColor=vec4(0.0,0.0,0.0,0.0);		
		}
}