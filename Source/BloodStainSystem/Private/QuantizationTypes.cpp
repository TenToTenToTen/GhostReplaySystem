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

	const float MinsArr[3] = { (float)Mins.X, (float)Mins.Y, (float)Mins.Z };
	const float RangesArr[3] = { (float)Ranges.X, (float)Ranges.Y, (float)Ranges.Z };

	const float ScaleMinsArr[3] = { (float)ScaleMins.X, (float)ScaleMins.Y, (float)ScaleMins.Z };
	const float ScaleRangesArr[3] = {
		FMath::Max((float)ScaleRangesVec.X, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Y, KINDA_SMALL_NUMBER),
		FMath::Max((float)ScaleRangesVec.Z, KINDA_SMALL_NUMBER)
	};
	
	Translation.FromVector(FVector3f(T.GetLocation()), MinsArr, RangesArr);
	Rotation.FromQuat(FQuat4f(T.GetRotation()));
	Scale = FVectorIntervalFixed32NoW(FVector3f(T.GetScale3D()), ScaleMinsArr, ScaleRangesArr);
}

FTransform FQuantizedTransform_Lowest::ToTransform(const FLocRange& Range, const FScaleRange& ScaleRange) const
{
	FTransform Out;
	
	FVector Mins = Range.PosMin;
	FVector Ranges = Range.PosMax - Mins;
	Ranges.X = FMath::Max(Ranges.X, KINDA_SMALL_NUMBER);
	Ranges.Y = FMath::Max(Ranges.Y, KINDA_SMALL_NUMBER);
	Ranges.Z = FMath::Max(Ranges.Z, KINDA_SMALL_NUMBER);

	const float MinsArr[3] = { (float)Mins.X, (float)Mins.Y, (float)Mins.Z };
    const float RangesArr[3] = { (float)Ranges.X, (float)Ranges.Y, (float)Ranges.Z };
    
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