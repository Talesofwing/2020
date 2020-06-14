#include "BasicShaderHeader.hlsli"

Texture2D<float4> tex : register(t0);		//0������åȤ��O�����줿�ƥ�������
SamplerState smp : register(s0);			//0������åȤ��O�����줿����ץ�

float4 main (Output input) : SV_TARGET {
	return float4(tex.Sample (smp, input.uv));
}