#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Math/Quat.h"
#include "AnimationCompression.h"
#include "QuantizationTypes.generated.h"

// 어떤 양자화 전략을 사용할지 선택하는 열거형
UENUM(BlueprintType)
enum class ETransformQuantizationMethod : uint8
{
	None,                // 양자화 안함 (FTransform, 48바이트)
	Standard_High,       // 표준 양자화 (FQuatFixed48 사용, 약 20바이트)
	Standard_Compact,    // 압축 양자화 (FQuatFixed32 사용, 약 18바이트)
};

// 양자화 옵션을 담는 구조체
USTRUCT(BlueprintType)
struct FQuantizationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization")
	ETransformQuantizationMethod Method = ETransformQuantizationMethod::Standard_High;
};


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