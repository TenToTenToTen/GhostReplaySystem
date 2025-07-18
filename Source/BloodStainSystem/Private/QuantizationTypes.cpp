/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "QuantizationTypes.h"

FQuantizedTransform_Lowest::FQuantizedTransform_Lowest(const FTransform& T, const FLocRange& BoneRange,const FScaleRange& ScaleRange)
{
	FVector Mins = BoneRange.PosMin;
	FVector Ranges = BoneRange.PosMax - Mins;

	FVector ScaleMins = ScaleRange.ScaleMin;
	FVector ScaleRangesVec = ScaleRange.ScaleMax - ScaleMins;
	
	Ranges.X = FMath::Max(Ranges.X, KINDA_SMALL_NUMBER);
	Ranges.Y = FMath::Max(Ranges.Y, KINDA_SMALL_NUMBER);
	Ranges.Z = FMath::Max(Ranges.Z, KINDA_SMALL_NUMBER);

	const float MinsArr[3] = {
		static_cast<float>(Mins.X), static_cast<float>(Mins.Y), static_cast<float>(Mins.Z)
	};
	const float RangesArr[3] = {
		static_cast<float>(Ranges.X), static_cast<float>(Ranges.Y), static_cast<float>(Ranges.Z)
	};

	const float ScaleMinsArr[3] = { (float)ScaleMins.X, (float)ScaleMins.Y, (float)ScaleMins.Z };
	const float ScaleRangesArr[3] = {
		FMath::Max((float)ScaleRangesVec.X, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Y, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Z, KINDA_SMALL_NUMBER)
	};

	FVector3f Vec3f(T.GetLocation());
	FQuat4f Quat4f(T.GetRotation());
	FVector3f Scale3f(T.GetScale3D());
	
	Translation.FromVector(Vec3f, MinsArr, RangesArr);
	Rotation.FromQuat(Quat4f);
	Scale = FVectorIntervalFixed32NoW(Scale3f, ScaleMinsArr, ScaleRangesArr);
}

FTransform FQuantizedTransform_Lowest::ToTransform(const FLocRange& Range, const FScaleRange& ScaleRange) const
{
	FTransform Out;
	
	FVector Mins = Range.PosMin;
	FVector Ranges = Range.PosMax - Mins;
	Ranges.X = FMath::Max(Ranges.X, KINDA_SMALL_NUMBER);
	Ranges.Y = FMath::Max(Ranges.Y, KINDA_SMALL_NUMBER);
	Ranges.Z = FMath::Max(Ranges.Z, KINDA_SMALL_NUMBER);

	const float MinsArr[3] = {
		static_cast<float>(Mins.X), static_cast<float>(Mins.Y), static_cast<float>(Mins.Z)
	};
	const float RangesArr[3] = {
		static_cast<float>(Ranges.X), static_cast<float>(Ranges.Y), static_cast<float>(Ranges.Z)
	};
    
    FVector3f Loc;
    Translation.ToVector(Loc, MinsArr, RangesArr);

	FQuat4f Rot;
	Rotation.ToQuat(Rot);
	
	Out.SetLocation(FVector(Loc));
	Out.SetRotation( FQuat(Rot) );

	FVector ScaleMins = ScaleRange.ScaleMin;
	FVector ScaleRangesVec = ScaleRange.ScaleMax - ScaleMins;
	const float ScaleMinsArr[3] = { (float)ScaleMins.X, (float)ScaleMins.Y, (float)ScaleMins.Z };
	const float ScaleRangesArr[3] = {
		FMath::Max((float)ScaleRangesVec.X, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Y, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Z, KINDA_SMALL_NUMBER)
	};
	
	FVector3f S3f;
	Scale.ToVector(S3f, ScaleMinsArr, ScaleRangesArr);
	
	Out.SetScale3D(FVector(S3f));

	return Out;
}