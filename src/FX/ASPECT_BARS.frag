#extension GL_ARB_texture_rectangle : require
uniform vec2 texCoord0;
uniform sampler2DRect theImage;

uniform float width;
uniform float height;

void main()
{	
		float Aspect = width/height;
		
		float topLimit = (texCoord0.y-(texCoord0.x/Aspect))/2.0;
		float bottomLimit = texCoord0.y - topLimit;
		
		if(gl_TexCoord[0].t<topLimit || gl_TexCoord[0].t>bottomLimit)
		{
			gl_FragColor=vec4(0.0,0.0,0.0,0.0);
		}
		else	
		{
			gl_FragColor = texture2DRect(theImage,gl_TexCoord[0].st);
		}
}