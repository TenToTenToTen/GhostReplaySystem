#pragma once

#include "CoreMinimal.h"

// 양자화/역양자화 로직을 위한 순수 가상 인터페이스
class BLOODSTAINSYSTEM_API ITransformQuantizer
{
public:
	virtual ~ITransformQuantizer() = default;

	// FTransform을 FArchive에 양자화하여 씁니다.
	virtual void Quantize(const FTransform& InTransform, FArchive& OutArchive) const = 0;

	// FArchive에서 데이터를 읽어 FTransform으로 역양자화합니다.
	virtual FTransform Dequantize(FArchive& InArchive) const = 0;
};