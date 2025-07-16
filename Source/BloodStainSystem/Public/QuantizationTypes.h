/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Math/Quat.h"
#include "AnimationCompression.h"
#include "GhostData.h"

/**
 * @brief Relatively High-precision quantized transform.
 *
 * Uses:
 *  - 0.01-unit quantization for Location (FVector_NetQuantize100),
 *  - 48-bit fixed-point rotation (FQuatFixed48NoW),
 *  - 0.1-unit quantization for Scale (FVector_NetQuantize10).
 */
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
		Ar << Data.Location;
		Ar << Data.Rotation;
		Ar << Data.Scale;
		return Ar;
	}
};

/**
 * @brief Standard compact quantized transform.
 *
 * Uses:
 *  - 0.01-unit quantization for Location (FVector_NetQuantize100),
 *  - 32-bit fixed-point rotation (FQuatFixed32NoW, 11/11/10 bits),
 *  - 0.1-unit quantization for Scale (FVector_NetQuantize10).
 */
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

/**
 * @brief Lowest-bit quantized transform.
 *
 * Uses:
 *  - interval‐based 32-bit fixed-point quantization for Translation (10 bits per axis),
 *  - 32-bit fixed-point rotation (FQuatFixed32NoW, 11/11/10 bits),
 *  - interval‐based 32-bit fixed-point quantization for Scale (8 bits per axis).
 */
struct FQuantizedTransform_Lowest
{
	FVectorIntervalFixed32NoW Translation;

	FQuatFixed32NoW           Rotation;

	FVectorIntervalFixed32NoW Scale;

	FQuantizedTransform_Lowest() = default;

	/** Quantize original FTransform into bitfields */
	FQuantizedTransform_Lowest(const FTransform& T, const FRange& Range, const FScaleRange& ScaleRange);

	/** Reconstruct FTransform from quantized bitfields */
	FTransform ToTransform(const FRange& Range, const FScaleRange& ScaleRange) const;

	friend FArchive& operator<<(FArchive& Ar, FQuantizedTransform_Lowest& Q)
	{
		Ar << Q.Translation;
		Ar << Q.Rotation;
		Ar << Q.Scale;
		return Ar;
	}
};