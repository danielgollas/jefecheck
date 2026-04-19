uniform sampler2DRect Top;
uniform sampler2DRect Bottom;
uniform float ShowMatte;
uniform float ShowClean;
uniform float Bias;
uniform float OffsetX;
uniform float OffsetY;
uniform float ScaleX;
uniform float ScaleY;
uniform float spillSupress;
uniform float astart;
uniform float range;

void main()
{	
		vec4 top=texture2DRect(Top,gl_TexCoord[1].st);		
		vec4 bottom=texture2DRect(Bottom,vec2((gl_TexCoord[0].s+OffsetX)/ScaleX,  (gl_TexCoord[0].t+OffsetY)/ScaleY));		
										
		float d= 2.0*top.b - top.r- top.g; //simplified distance function
			
		float clipMatte;
		clipMatte=smoothstep(astart,astart+range,d);
	
		top = vec4(top.r,top.g,min(top.b,top.g),top.a)*spillSupress + top*((spillSupress-1.0)*(spillSupress-1.0));
		
		if(ShowClean==1.0)
		{
			gl_FragColor=top*(1.0-clipMatte);
		}
		else if(ShowMatte==1.0){
		gl_FragColor = vec4(clipMatte);
		}
		else
		{
			gl_FragColor=bottom*clipMatte+top*(1.0-clipMatte);
		}
}