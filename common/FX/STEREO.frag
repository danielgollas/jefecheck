uniform sampler2DRect Left;
uniform sampler2DRect Right;
uniform float Red;
uniform float Cyan;
const vec3 lumCoeff = vec3(0.2125,0.7154,0.0721);
void main()
{	
	
		vec4 left=texture2DRect(Left,gl_TexCoord[0].st);		
		vec4 right=texture2DRect(Right,gl_TexCoord[1].st);
		
        left=vec4(dot(left.rgb,lumCoeff));
        right=vec4(dot(right.rgb,lumCoeff));
		left=(left*vec4(Red,0,0,0));
		right=(right*vec4(0,Cyan,Cyan,0));
        gl_FragColor=vec4(left.r, right.g, right.b, 1.0);
		//gl_FragColor =min(left,right);
		
		
	
}
