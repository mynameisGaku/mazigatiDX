#include "Matrix4x4.h"


bool FMatrix4x4::TryInverse( FMatrix4x4& OutInverse ) const
{
	const float32 s0 = m[0][0] * m[1][1] - m[1][0] * m[0][1];
	const float32 s1 = m[0][0] * m[1][2] - m[1][0] * m[0][2];
	const float32 s2 = m[0][0] * m[1][3] - m[1][0] * m[0][3];
	const float32 s3 = m[0][1] * m[1][2] - m[1][1] * m[0][2];
	const float32 s4 = m[0][1] * m[1][3] - m[1][1] * m[0][3];
	const float32 s5 = m[0][2] * m[1][3] - m[1][2] * m[0][3];
	const float32 c5 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
	const float32 c4 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
	const float32 c3 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
	const float32 c2 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
	const float32 c1 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
	const float32 c0 = m[2][0] * m[3][1] - m[3][0] * m[2][1];

	const float32 Det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
	if ( NMath::Abs( Det ) <= NMath::SmallNumber ) return false;

	const float32 InvDet = 1.0f / Det;
	FMatrix4x4 R;
	R.m[0][0] = (  m[1][1] * c5 - m[1][2] * c4 + m[1][3] * c3 ) * InvDet;
	R.m[0][1] = ( -m[0][1] * c5 + m[0][2] * c4 - m[0][3] * c3 ) * InvDet;
	R.m[0][2] = (  m[3][1] * s5 - m[3][2] * s4 + m[3][3] * s3 ) * InvDet;
	R.m[0][3] = ( -m[2][1] * s5 + m[2][2] * s4 - m[2][3] * s3 ) * InvDet;

	R.m[1][0] = ( -m[1][0] * c5 + m[1][2] * c2 - m[1][3] * c1 ) * InvDet;
	R.m[1][1] = (  m[0][0] * c5 - m[0][2] * c2 + m[0][3] * c1 ) * InvDet;
	R.m[1][2] = ( -m[3][0] * s5 + m[3][2] * s2 - m[3][3] * s1 ) * InvDet;
	R.m[1][3] = (  m[2][0] * s5 - m[2][2] * s2 + m[2][3] * s1 ) * InvDet;

	R.m[2][0] = (  m[1][0] * c4 - m[1][1] * c2 + m[1][3] * c0 ) * InvDet;
	R.m[2][1] = ( -m[0][0] * c4 + m[0][1] * c2 - m[0][3] * c0 ) * InvDet;
	R.m[2][2] = (  m[3][0] * s4 - m[3][1] * s2 + m[3][3] * s0 ) * InvDet;
	R.m[2][3] = ( -m[2][0] * s4 + m[2][1] * s2 - m[2][3] * s0 ) * InvDet;

	R.m[3][0] = ( -m[1][0] * c3 + m[1][1] * c1 - m[1][2] * c0 ) * InvDet;
	R.m[3][1] = (  m[0][0] * c3 - m[0][1] * c1 + m[0][2] * c0 ) * InvDet;
	R.m[3][2] = ( -m[3][0] * s3 + m[3][1] * s1 - m[3][2] * s0 ) * InvDet;
	R.m[3][3] = (  m[2][0] * s3 - m[2][1] * s1 + m[2][2] * s0 ) * InvDet;

	OutInverse = R;
	return true;
}
