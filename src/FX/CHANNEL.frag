uniform sampler2DRect image;
uniform float channel;

void main()
{	
	vec4 color=texture2DRect(image,gl_TexCoord[0].st);

	if(channel==0.0){
		gl_FragColor=vec4(color.r, color.r, color.r, color.a);
		
	}else if(channel==1.0){
		gl_FragColor=vec4(color.g, color.g, color.g, color.a);
		
	}else if(channel==2.0){
		gl_FragColor=vec4(color.b, color.b, color.b, color.a);
		
	}else if(channel==3.0){
		gl_FragColor=vec4(color.a, color.a, color.a, color.a);
		
	}
	
	
	/*gl_FragColor =vec4(R*color.r + G*color.g + B*color.b + A*color.a,
				R*color.r + G*color.g + B*color.b + A*color.a,
				R*color.r + G*color.g + B*color.b + A*color.a,
				color.a);*/
	//gl_FragColor =vec4(R,G,B,A);

}