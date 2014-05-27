uniform sampler2DRect image;
uniform float k00,k01,k02, k10,k11,k12, k20,k21,k22;
uniform float presets;
void main()
{		
		
		vec4 c11,c12,c13;
		vec4 c21,c22,c23;
		vec4 c31,c32,c33;
		vec4 c=gl_TexCoord[0];
		//get all the neighbouring pixels
	
		//float kernel[9]={0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625};
		
		mat3 kernelSharpen=mat3(-1.0,-1.0,-1.0, -1.0,9.0,-1.0, -1.0,-1.0,-1.0);
		mat3 kernelGauss=mat3(0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625);
		mat3 kernelMean=mat3(1.0/9.0,1.0/9.0,1.0/9.0, 1.0/9.0,1.0/9.0,1.0/9.0, 1.0/9.0,1.0/9.0,1.0/9.0);
		mat3 kernelLaplace=mat3(-1.0/8.0,-1.0/8.0,-1.0/8.0, -1.0/8.0,1.0,-1.0/8.0, -1.0/8.0,-1.0/8.0,-1.0/8.0);
		mat3 kernelEmboss=mat3(2.0,0.0,0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0);
		mat3 kernelCustom=mat3(k00,k01,k02,k10,k11,k12,k20,k21,k22);
		
		/*mat3 kernels[6];
		kernels[0]=mat3(-1.0,-1.0,-1.0, -1.0,9.0,-1.0, -1.0,-1.0,-1.0);
		kernels[1]=mat3(0.0625,0.125,0.0625,0.125,0.25,0.125,0.0625,0.125,0.0625);
		kernels[2]=mat3(1.0/9.0,1.0/9.0,1.0/9.0, 1.0/9.0,1.0/9.0,1.0/9.0, 1.0/9.0,1.0/9.0,1.0/9.0);
		kernels[3]=mat3(-1.0/8.0,-1.0/8.0,-1.0/8.0, -1.0/8.0,1.0,-1.0/8.0, -1.0/8.0,-1.0/8.0,-1.0/8.0);
		kernels[4]=mat3(2.0,0.0,0.0,  0.0, -1.0, 0.0,  0.0, 0.0, -1.0);
		kernels[5]=mat3(k00,k01,k02,k10,k11,k12,k20,k21,k22);
		
		int index=int(presets);
		
		mat3 kernel=kernels[index];*/
		mat3 kernel;
		
		if(presets==0.0){
		kernel = kernelSharpen;
		}else{
			if(presets==1.0){
			kernel = kernelGauss;
			}else{
				if(presets==2.0){
				kernel = kernelMean;
				}else{
					if(presets==3.0){
					kernel = kernelLaplace;
					}else{
						if(presets==4.0){
						kernel = kernelEmboss;
						}else{
							kernel = kernelCustom;
						}
					}
				}
			}
		}
		
		
		
		c11=texture2DRect(image,vec2(c.s-1.0,c.t-1.0))*kernel[0][0];
		c12=texture2DRect(image,vec2(c.s-1.0,c.t))*kernel[0][1];
		c13=texture2DRect(image,vec2(c.s-1.0,c.t+1.0))*kernel[0][2];

		c21=texture2DRect(image,vec2(c.s,c.t-1.0))*kernel[1][0];
		c22=texture2DRect(image,vec2(c.s,c.t))*kernel[1][1];
		c23=texture2DRect(image,vec2(c.s,c.t+1.0))*kernel[1][2];

		c31=texture2DRect(image,vec2(c.s+1.0,c.t-1.0))*kernel[2][0];
		c32=texture2DRect(image,vec2(c.s+1.0,c.t))*kernel[2][1];
		c33=texture2DRect(image,vec2(c.s+1.0,c.t+1.0))*kernel[2][2];

		gl_FragColor =c11+c12+c13+c21+c22+c23+c31+c32+c33;


}