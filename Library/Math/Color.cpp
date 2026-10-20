#include "Color.h"


FColor FColor::FromHSV( float32 H, float32 S, float32 V, float32 A )
{
	const float32 C  = V * S;
	const float32 Hp = NMath::Fmod( NMath::Fmod( H, 360.0f ) + 360.0f, 360.0f ) / 60.0f;
	const float32 X  = C * ( 1.0f - NMath::Abs( NMath::Fmod( Hp, 2.0f ) - 1.0f ) );
	const float32 M  = V - C;

	float32 R = 0.0f, G = 0.0f, B = 0.0f;
	if      ( Hp < 1.0f ) { R = C; G = X; }
	else if ( Hp < 2.0f ) { R = X; G = C; }
	else if ( Hp < 3.0f ) { G = C; B = X; }
	else if ( Hp < 4.0f ) { G = X; B = C; }
	else if ( Hp < 5.0f ) { R = X; B = C; }
	else                  { R = C; B = X; }
	return { R + M, G + M, B + M, A };
}
