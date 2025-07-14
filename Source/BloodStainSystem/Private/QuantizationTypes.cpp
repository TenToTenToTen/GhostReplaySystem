#include "QuantizationTypes.h"

static constexpr float LOC_MINS[3]   = { -5000.f, -5000.f, -5000.f };
static constexpr float LOC_RANGES[3] = { 10000.f, 10000.f, 10000.f };

// Scale: 0…4
static constexpr float SCL_MINS[3]   = { 0.f, 0.f, 0.f };
static constexpr float SCL_RANGES[3] = { 4.f, 4.f, 4.f };

FQuantizedTransform_Lowest::FQuantizedTransform_Lowest(const FTransform& T)
	: Translation( FVector3f(T.GetLocation()), LOC_MINS, LOC_RANGES )
	, Rotation(   FQuat4f(T.GetRotation()) )
	, Scale(      FVector3f(T.GetScale3D()),   SCL_MINS, SCL_RANGES )
{}

FQuantizedTransform_Lowest::FQuantizedTransform_Lowest(const FTransform& T, const FBoneRange& BoneRange)
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

FTransform FQuantizedTransform_Lowest::ToTransform() const
{
	FTransform Out;

	// 1) 위치 복원
	FVector3f Loc3f;
	Translation.ToVector( Loc3f, LOC_MINS, LOC_RANGES );
	Out.SetLocation(FVector(Loc3f));

	// 2) 회전 복원
	FQuat4f Q = FQuatFixed32NoW::ToQuat<true>( &Rotation.Packed );
	Out.SetRotation( FQuat(Q) );

	// 3) 스케일 복원
	FVector3f S3f;
	Scale.ToVector( S3f, SCL_MINS, SCL_RANGES );
	Out.SetScale3D(FVector(S3f));

	return Out;
}

FTransform FQuantizedTransform_Lowest::ToTransform(const FBoneRange& BoneRange) const
{
	FTransform Out;
	
	// 위치: 전달받은 BoneRange 사용
	FVector Mins = BoneRange.PosMin;
	FVector Ranges = BoneRange.PosMax - Mins;
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

	// return FTransform(FQuat(Rot), FVector(Loc), FVector::OneVector);
	return Out;
}

FArchive& operator<<(FArchive& Ar, FQuantizedTransform_Lowest& Q)
{
	Ar << Q.Translation;
	Ar << Q.Rotation;
	Ar << Q.Scale;
	return Ar;
}