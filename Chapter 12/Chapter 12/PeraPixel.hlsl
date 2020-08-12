#include "PeraHeader.hlsli"

float4 main (Output input) : SV_TARGET {
	float4 col = tex.Sample (smp, input.uv);
	//return float4 (input.uv, 1, 1);
	//return tex.Sample (smp, input.uv);

	//
	// ���m�N��
	//
	//float Y = dot(col.rgb, float3(0.299, 0.587, 0.114));
	//return float4 (Y, Y, Y, 1);

	//
	// �F�̔��]
	//
	//float3 c = float3 (1.0, 1.0, 1.0) - col.rgb;
	//return float4 (c, col.a);

	//
	// �F�̊K���𗎂Ƃ�
	//
	//return float4 (col.rgb - fmod(col.rgb, 0.value5f), col.a);

	float w, h, levels;
	tex.GetDimensions (0, w, h, levels);

	float dx = 1.0f / w;
	float dy = 1.0f / h;
	float4 ret = float4 (0, 0, 0, 0);

	int value = 2;

	//
	// �ڂ�������
	//
	//ret += tex.Sample (smp, input.uv + float2(-value * dx, -value * dy));	// ����
	//ret += tex.Sample (smp, input.uv + float2(0, -value * dy));				// ��
	//ret += tex.Sample (smp, input.uv + float2(value * dx, -value * dy));	// �E��
	//ret += tex.Sample (smp, input.uv + float2(-value * dx, 0));				// ��
	//ret += tex.Sample (smp, input.uv);										// ����
	//ret += tex.Sample (smp, input.uv + float2(value * dx, 0));				// �E
	//ret += tex.Sample (smp, input.uv + float2(-value * dx, value * dy));	// ����
	//ret += tex.Sample (smp, input.uv + float2(0, value * dy));				// ��
	//ret += tex.Sample (smp, input.uv + float2(value * dx, value * dy));		// �E��

	//return ret / 9.0f;

	//
	// �G���{�X���H
	// 
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, -value * dy)) * 2;	// ����
	//ret += tex.Sample (smp, input.uv + float2 (0, -value * dy));				// ��
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, -value * dy)) * 0;	// �E��
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, 0));				// ��
	//ret += tex.Sample (smp, input.uv);											// ����
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, 0)) * -1;			// �E
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, value * dy)) * 0;	// ����
	//ret += tex.Sample (smp, input.uv + float2 (0, value * dy)) * -1;			// ��
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, value * dy)) * -2;	// �E��

	//float Y = dot (ret.rgb, float3(0.299, 0.587, 0.114));
	//return float4 (Y, Y, Y, 1);

	// return ret;

	//
	// �V���[�v�l�X�i�G�b�W�̋����j
	//
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, -value * dy)) * 0;	// ����
	//ret += tex.Sample (smp, input.uv + float2 (0, -value * dy)) * -1;			// ��
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, -value * dy)) * 0;	// �E��
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, 0)) * -1;			// ��
	//ret += tex.Sample (smp, input.uv) * 5;										// ����
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, 0)) * -1;			// �E
	//ret += tex.Sample (smp, input.uv + float2 (-value * dx, value * dy)) * 0;	// ����
	//ret += tex.Sample (smp, input.uv + float2 (0, value * dy)) * -1;			// ��
	//ret += tex.Sample (smp, input.uv + float2 (value * dx, value * dy)) * 0;	// �E��

	//return ret;

	//
	// �ȒP�ȗ֊s��
	//
	ret += tex.Sample (smp, input.uv + float2 (-value * dx, -value * dy)) * 0;	// ����
	ret += tex.Sample (smp, input.uv + float2 (0, -value * dy)) * -1;			// ��
	ret += tex.Sample (smp, input.uv + float2 (value * dx, -value * dy)) * 0;	// �E��
	ret += tex.Sample (smp, input.uv + float2 (-value * dx, 0)) * -1;			// ��
	ret += tex.Sample (smp, input.uv) * 4;										// ����
	ret += tex.Sample (smp, input.uv + float2 (value * dx, 0)) * -1;			// �E
	ret += tex.Sample (smp, input.uv + float2 (-value * dx, value * dy)) * 0;	// ����
	ret += tex.Sample (smp, input.uv + float2 (0, value * dy)) * -1;			// ��
	ret += tex.Sample (smp, input.uv + float2 (value * dx, value * dy)) * 0;	// �E��

	// ���]
	float Y = dot (ret.rgb, float3 (0.299, 0.587, 0.114));

	Y = pow (1.0f - Y, 10.0f);
	Y = step (0.2, Y);

	return float4 (Y, Y, Y, col.a);
}
