#pragma once

#include "ITransformQuantizer.h"
#include "QuantizationTypes.h"

// 고정밀도(FQuatFixed48) 전략 클래스
class BLOODSTAINSYSTEM_API FHighPrecisionQuantizer : public ITransformQuantizer
{
public:
	virtual void Quantize(const FTransform& InTransform, FArchive& OutArchive) const override
	{
		FQuantizedTransform_High QuantizedData(InTransform);
		OutArchive.Serialize(&QuantizedData, sizeof(FQuantizedTransform_High));
	}

	virtual FTransform Dequantize(FArchive& InArchive) const override
	{
		FQuantizedTransform_High QuantizedData;
		InArchive.Serialize(&QuantizedData, sizeof(FQuantizedTransform_High));
		return QuantizedData.ToTransform();
	}
};

// 압축(FQuatFixed32) 전략 클래스
class BLOODSTAINSYSTEM_API FCompactQuantizer : public ITransformQuantizer
{
public:
	virtual void Quantize(const FTransform& InTransform, FArchive& OutArchive) const override
	{
		FQuantizedTransform_Compact QuantizedData(InTransform);
		OutArchive.Serialize(&QuantizedData, sizeof(FQuantizedTransform_Compact));
	}

	virtual FTransform Dequantize(FArchive& InArchive) const override
	{
		FQuantizedTransform_Compact QuantizedData;
		InArchive.Serialize(&QuantizedData, sizeof(FQuantizedTransform_Compact));
		return QuantizedData.ToTransform();
	}
};

// 양자화를 하지 않는 전략 클래스 (None 옵션용)
class BLOODSTAINSYSTEM_API FNullQuantizer : public ITransformQuantizer
{
public:
	virtual void Quantize(const FTransform& InTransform, FArchive& OutArchive) const override
	{
		// FTransform은 << 연산자가 정의되어 있으므로 그대로 사용
		const_cast<FTransform&>(InTransform).Serialize(OutArchive);
	}

	virtual FTransform Dequantize(FArchive& InArchive) const override
	{
		FTransform DequantizedTransform;
		DequantizedTransform.Serialize(InArchive);
		return DequantizedTransform;
	}
};