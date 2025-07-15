#include "QuantizationTypes.h"

static constexpr float SCL_MINS[3]   = { 0.f, 0.f, 0.f };
static constexpr float SCL_RANGES[3] = { 10.f, 10.f, 10.f };

FQuantizedTransform_Lowest::FQuantizedTransform_Lowest(const FTransform& T, const FRange& BoneRange)
{
	// 위치: 전달받은 BoneRange 사용
	FVector Mins = BoneRange.PosMin;
	FVector Ranges = BoneRange.PosMax - Mins;
	Ranges.X = FMath::Max(Ranges.X, KINDA_SMALL_NUMBER);
	Ranges.Y = FMath::Max(Ranges.Y, KINDA_SMALL_NUMBER);
	Ranges.Z = FMath::Max(Ranges.Z, KINDA_SMALL_NUMBER);

	const float MinsArr[3] = { (float)Mins.X, (float)Mins.Y, (float)Mins.Z };
	const float RangesArr[3] = { (float)Ranges.X, (float)Ranges.Y, (float)Ranges.Z };

	Translation.FromVector(FVector3f(T.GetLocation()), MinsArr, RangesArr);
	Rotation.FromQuat(FQuat4f(T.GetRotation()));
	Scale = FVectorIntervalFixed32NoW(FVector3f(T.GetScale3D()), SCL_MINS, SCL_RANGES);
	// Scale.Packed = 0;
}

FTransform FQuantizedTransform_Lowest::ToTransform(const FRange& Range) const
{
	FTransform Out;
	
	// 위치: 전달받은 Range 사용
	FVector Mins = Range.PosMin;
	FVector Ranges = Range.PosMax - Mins;
	Ranges.X = FMath::Max(Ranges.X, KINDA_SMALL_NUMBER);
	Ranges.Y = FMath::Max(Ranges.Y, KINDA_SMALL_NUMBER);
	Ranges.Z = FMath::Max(Ranges.Z, KINDA_SMALL_NUMBER);

	const float MinsArr[3] = { (float)Mins.X, (float)Mins.Y, (float)Mins.Z };
    const float RangesArr[3] = { (float)Ranges.X, (float)Ranges.Y, (float)Ranges.Z };
    
    FVector3f Loc;
    Translation.ToVector(Loc, MinsArr, RangesArr);

	// 회전: 기존과 동일
	FQuat4f Rot;
	Rotation.ToQuat(Rot);

	FVector3f S3f;
	Scale.ToVector( S3f, SCL_MINS, SCL_RANGES );
	
	Out.SetLocation(FVector(Loc));
	Out.SetRotation( FQuat(Rot) );
	Out.SetScale3D(FVector(S3f));

	return Out;
}