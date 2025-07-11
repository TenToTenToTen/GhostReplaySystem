#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Math/Quat.h"
#include "AnimationCompression.h"

// 표준-고정밀도 양자화 트랜스폼 (FQuatFixed48 사용)
struct FQuantizedTransform_High
{
	FVector_NetQuantize100 Location;
	
	FQuatFixed48NoW Rotation;
	
	FVector_NetQuantize10 Scale;

	FQuantizedTransform_High() = default;

	explicit FQuantizedTransform_High(const FTransform& T) 
		: Location(T.GetLocation())
		, Rotation(FQuat4f(T.GetRotation()))
		, Scale(T.GetScale3D()) 
	{}
	
	FTransform ToTransform() const
	{
		FTransform T;
		T.SetLocation(Location);
		FQuat4f TempQuat;
		Rotation.ToQuat(TempQuat);
		T.SetRotation(FQuat(TempQuat));
		T.SetScale3D(Scale);
		return T;
	}
};

// 표준-압축 양자화 트랜스폼 (FQuatFixed32 사용)
struct FQuantizedTransform_Compact
{
	FVector_NetQuantize100 Location;
	
	FQuatFixed32NoW Rotation;
	
	FVector_NetQuantize10 Scale;

	FQuantizedTransform_Compact() = default;

	explicit FQuantizedTransform_Compact(const FTransform& T) 
		: Location(T.GetLocation())
		, Rotation(FQuat4f(T.GetRotation())) 
		, Scale(T.GetScale3D()) 
	{}
	
	FTransform ToTransform() const
	{
		FTransform T;
		T.SetLocation(Location);
		FQuat4f TempQuat;
		Rotation.ToQuat(TempQuat);
		T.SetRotation(FQuat(TempQuat));
		T.SetScale3D(Scale);
		return T;
	}
};