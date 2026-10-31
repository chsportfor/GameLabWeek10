#pragma once

#include "Math/Vector4.h"
#include "Math/Vector2.h"
#include "Math/Matrix.h"
#include "Math/Color.h"

struct FDecalInfo
{
	FMatrix			WorldToDecal;
	FLinearColor	DecalColor = FLinearColor(1,1,1,1);
	FVector2		ViewSize;
	float			Padding[2];
};