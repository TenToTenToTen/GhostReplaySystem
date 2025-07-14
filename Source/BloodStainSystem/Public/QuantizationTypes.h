#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Math/Quat.h"
#include "AnimationCompression.h"
#include "GhostData.h"

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
	
	friend FArchive& operator<<(FArchive& Ar, FQuantizedTransform_High& Data)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Serializing FQuantizedTransform_High"));
		Ar << Data.Location;
		Ar << Data.Rotation;
		Ar << Data.Scale;
		return Ar;
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

	friend FArchive& operator<<(FArchive& Ar, FQuantizedTransform_Compact& Data)
	{
		Ar << Data.Location;
		Ar << Data.Rotation;
		Ar << Data.Scale;
		return Ar;
	}
};

struct FQuantizedTransform_Lowest
{

	// 위치: IntervalFixed32NoW (10bit/축)  
	FVectorIntervalFixed32NoW Translation;

	// 회전: Fixed32NoW (11/11/10 bit)  
	FQuatFixed32NoW           Rotation;

	// 스케일: IntervalFixed32NoW (8bit/축)  
	FVectorIntervalFixed32NoW Scale;

	FQuantizedTransform_Lowest() = default;

	/** 원본 FTransform → 양자화 비트필드 */
	explicit FQuantizedTransform_Lowest(const FTransform& T);
	FQuantizedTransform_Lowest(const FTransform& T, const FBoneRange& BoneRange);

	/** 양자화된 비트필드를 FTransform으로 복원 */
	FTransform ToTransform() const;
	FTransform ToTransform(const FBoneRange& BoneRange) const;

	/** Archive << 연산자 (순서대로 Translation, Rotation, Scale) */
	friend FArchive& operator<<(FArchive& Ar, FQuantizedTransform_Lowest& Q);
};