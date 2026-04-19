void main()
	{	
		gl_TexCoord[0] = gl_MultiTexCoord0;
		gl_TexCoord[1] = gl_MultiTexCoord1;
		gl_TexCoord[2] = gl_MultiTexCoord2;
		gl_TexCoord[3] = gl_MultiTexCoord3;
		//gl_Position = ftransform();
		vec4 Vertex=gl_Vertex;
		mat4 scaleMat=mat4(-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
		gl_Position = gl_ModelViewProjectionMatrix *scaleMat*Vertex;
	}